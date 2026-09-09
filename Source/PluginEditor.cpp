#include "PluginEditor.h"

JungleStretchAudioProcessorEditor::JungleStretchAudioProcessorEditor (JungleStretchAudioProcessor& p)
    : AudioProcessorEditor (&p), processorRef (p), genericEditor (p)
{
    addAndMakeVisible (genericEditor);
    setSize (400, 400);
}

void JungleStretchAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));
}

void JungleStretchAudioProcessorEditor::resized()
{
    genericEditor.setBounds (getLocalBounds());
}
