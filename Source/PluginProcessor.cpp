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

    // Mode is limited to Stretch/Drag for now — Stumble, Turnaround and
    // Half-Time Drop are validated offline first before being added here.
    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { modeParamId, 1 }, "Mode",
        juce::StringArray { "Stretch", "Drag" }, 0));

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

    return { params.begin(), params.end() };
}

void JungleStretchAudioProcessor::prepareToPlay (double, int)
{
    // Ring buffer / DSP state is added when the algorithm is ported (not yet).
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

    // Passthrough scaffold — the grain-wander algorithm is ported in a later step.
    for (auto channel = getTotalNumInputChannels(); channel < getTotalNumOutputChannels(); ++channel)
        buffer.clear (channel, 0, buffer.getNumSamples());
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
            apvts.replaceState (juce::ValueTree::fromXml (*xmlState));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new JungleStretchAudioProcessor();
}
