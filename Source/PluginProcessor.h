#pragma once

#include <JuceHeader.h>
#include "GrainWanderEngine.h"

class JungleStretchAudioProcessor : public juce::AudioProcessor
{
public:
    JungleStretchAudioProcessor();
    ~JungleStretchAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // Parameter IDs, shared with the editor.
    static constexpr auto modeParamId = "mode";
    static constexpr auto intensityParamId = "intensity";
    static constexpr auto loopLengthParamId = "loopLength";
    static constexpr auto chopRateParamId = "chopRate";
    static constexpr auto mixParamId = "mix";
    static constexpr auto triggerWindowParamId = "triggerWindow";
    static constexpr auto triggerChanceParamId = "triggerChance";
    static constexpr auto manualBpmParamId = "manualBpm";
    static constexpr auto pitchParamId = "pitch";
    static constexpr auto pitchModeParamId = "pitchMode";
    static constexpr auto singularityParamId = "singularity";
    static constexpr auto singularityModeParamId = "singularityMode";

    juce::AudioProcessorValueTreeState apvts;

    // True once setStateInformation() has restored a saved DAW state. The editor
    // uses this to tell a genuinely fresh instance apart from a restored one, so
    // it only auto-loads the first named preset on a true first open.
    bool hasRestoredState = false;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    GrainWanderEngine engine;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (JungleStretchAudioProcessor)
};
