#include "PluginProcessor.h"
#include "PluginEditor.h"

JungleStretchAudioProcessor::JungleStretchAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
{
}

JungleStretchAudioProcessor::~JungleStretchAudioProcessor() = default;

juce::AudioProcessorValueTreeState::ParameterLayout JungleStretchAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    // Stumble, Turnaround and Half-Time Drop were validated offline first
    // (see Prototyping/jungle_stretch_prototype.py's render_position_gated()
    // and Output/jungle-stretch/position_gated/) before being added here.
    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { modeParamId, 1 }, "Mode",
        juce::StringArray { "Stretch", "Drag", "Stumble", "Turnaround", "Half-Time Drop" }, 0));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { intensityParamId, 1 }, "Intensity",
        juce::NormalisableRange<float> (0.0f, 100.0f), 40.0f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { loopLengthParamId, 1 }, "Loop Length",
        juce::NormalisableRange<float> (500.0f, 8000.0f), 4000.0f, "ms"));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { chopRateParamId, 1 }, "Chop Rate",
        juce::NormalisableRange<float> (0.0f, 100.0f), 50.0f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { mixParamId, 1 }, "Mix",
        juce::NormalisableRange<float> (0.0f, 100.0f), 100.0f, "%"));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { triggerWindowParamId, 1 }, "Trigger Window",
        juce::NormalisableRange<float> (0.0f, 100.0f), 25.0f, "%"));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { triggerChanceParamId, 1 }, "Trigger Chance",
        juce::NormalisableRange<float> (0.0f, 100.0f), 100.0f, "%"));

    // Fallback tempo for Drag's bar/beat gating when the host provides no playhead tempo.
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { manualBpmParamId, 1 }, "Manual BPM",
        juce::NormalisableRange<float> (60.0f, 200.0f), 120.0f, "BPM"));

    // Live "rough" pitching (turntable-style, not pitch-preserving).
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { pitchParamId, 1 }, "Pitch",
        juce::NormalisableRange<float> (-50.0f, 50.0f), 0.0f, "%"));

    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { pitchModeParamId, 1 }, "Pitch Mode",
        juce::StringArray { "Whole Signal", "Grain Only" }, 0));

    // "Crazy mode" performance toggle — freezes and progressively slows the
    // last moment of live input toward a near-static drone while held.
    params.push_back (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { singularityParamId, 1 }, "Singularity", false));

    // Flavour of the Singularity freeze — validated offline first (see
    // Prototyping/singularity_whitehole_prototype.py and
    // Output/singularity-whitehole/) before being added here.
    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { singularityModeParamId, 1 }, "Singularity Mode",
        juce::StringArray { "Black Hole", "Grey Hole", "White Hole" }, 0));

    return { params.begin(), params.end() };
}

void JungleStretchAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate, samplesPerBlock, getTotalNumOutputChannels());
}

void JungleStretchAudioProcessor::releaseResources()
{
}

bool JungleStretchAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo()
        && layouts.getMainInputChannelSet() == juce::AudioChannelSet::stereo();
}

void JungleStretchAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    GrainWanderEngine::Parameters params;
    {
        const int modeIndex = (int) std::round (apvts.getRawParameterValue (modeParamId)->load());
        switch (modeIndex)
        {
            case 1: params.mode = GrainWanderEngine::Mode::drag; break;
            case 2: params.mode = GrainWanderEngine::Mode::stumble; break;
            case 3: params.mode = GrainWanderEngine::Mode::turnaround; break;
            case 4: params.mode = GrainWanderEngine::Mode::halfTimeDrop; break;
            default: params.mode = GrainWanderEngine::Mode::stretch; break;
        }
    }
    params.intensity01 = apvts.getRawParameterValue (intensityParamId)->load() / 100.0f;
    params.loopLengthMs = apvts.getRawParameterValue (loopLengthParamId)->load();
    params.chopRate01 = apvts.getRawParameterValue (chopRateParamId)->load() / 100.0f;
    params.mix01 = apvts.getRawParameterValue (mixParamId)->load() / 100.0f;
    params.triggerWindow01 = apvts.getRawParameterValue (triggerWindowParamId)->load() / 100.0f;
    params.triggerChance01 = apvts.getRawParameterValue (triggerChanceParamId)->load() / 100.0f;
    params.manualBpm = (double) apvts.getRawParameterValue (manualBpmParamId)->load();
    params.pitchPercent = apvts.getRawParameterValue (pitchParamId)->load();
    params.pitchMode = apvts.getRawParameterValue (pitchModeParamId)->load() < 0.5f
                           ? GrainWanderEngine::PitchMode::wholeSignal
                           : GrainWanderEngine::PitchMode::grainOnly;
    params.singularityEngaged = apvts.getRawParameterValue (singularityParamId)->load() > 0.5f;
    {
        const int singularityModeIndex = (int) std::round (apvts.getRawParameterValue (singularityModeParamId)->load());
        switch (singularityModeIndex)
        {
            case 1: params.singularityMode = GrainWanderEngine::SingularityMode::greyHole; break;
            case 2: params.singularityMode = GrainWanderEngine::SingularityMode::whiteHole; break;
            default: params.singularityMode = GrainWanderEngine::SingularityMode::blackHole; break;
        }
    }

    engine.process (buffer, params, getPlayHead());
}

juce::AudioProcessorEditor* JungleStretchAudioProcessor::createEditor()
{
    return new JungleStretchAudioProcessorEditor (*this);
}

bool JungleStretchAudioProcessor::hasEditor() const
{
    return true;
}

const juce::String JungleStretchAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool JungleStretchAudioProcessor::acceptsMidi() const
{
    return false;
}

bool JungleStretchAudioProcessor::producesMidi() const
{
    return false;
}

bool JungleStretchAudioProcessor::isMidiEffect() const
{
    return false;
}

double JungleStretchAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int JungleStretchAudioProcessor::getNumPrograms()
{
    return 1;
}

int JungleStretchAudioProcessor::getCurrentProgram()
{
    return 0;
}

void JungleStretchAudioProcessor::setCurrentProgram (int)
{
}

const juce::String JungleStretchAudioProcessor::getProgramName (int)
{
    return {};
}

void JungleStretchAudioProcessor::changeProgramName (int, const juce::String&)
{
}

void JungleStretchAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto state = apvts.copyState(); true)
    {
        std::unique_ptr<juce::XmlElement> xml (state.createXml());
        copyXmlToBinary (*xml, destData);
    }
}

void JungleStretchAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (std::unique_ptr<juce::XmlElement> xmlState (getXmlFromBinary (data, sizeInBytes)); xmlState != nullptr)
        if (xmlState->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (juce::ValueTree::fromXml (*xmlState));
            hasRestoredState = true;
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new JungleStretchAudioProcessor();
}
