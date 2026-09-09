#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

// Generic parameter editor for now — a custom UI is designed in a later step.
class JungleStretchAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit JungleStretchAudioProcessorEditor (JungleStretchAudioProcessor&);
    ~JungleStretchAudioProcessorEditor() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    JungleStretchAudioProcessor& processorRef;
    juce::GenericAudioProcessorEditor genericEditor;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (JungleStretchAudioProcessorEditor)
};
