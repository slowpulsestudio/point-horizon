#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "Components/PresetToolbar.h"

// Generic parameter editor for now — a custom UI is designed in a later step.
class JungleStretchAudioProcessorEditor : public juce::AudioProcessorEditor,
                                           private juce::AudioProcessorValueTreeState::Listener,
                                           private juce::AsyncUpdater
{
public:
    explicit JungleStretchAudioProcessorEditor (JungleStretchAudioProcessor&);
    ~JungleStretchAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void parameterChanged (const juce::String& parameterID, float newValue) override;
    void handleAsyncUpdate() override;
    void selectPreset (int index);

    JungleStretchAudioProcessor& processorRef;
    sps::PresetToolbar presetToolbar;
    juce::GenericAudioProcessorEditor genericEditor;
    int selectedPresetIndex = 0;

    static constexpr int toolbarHeight = 28;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (JungleStretchAudioProcessorEditor)
};
