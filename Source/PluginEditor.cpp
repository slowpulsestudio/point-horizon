#include "PluginEditor.h"
#include "PresetManager.h"

namespace
{
    // Parameter changes that should refresh the preset toolbar's dirty indicator.
    constexpr const char* trackedParamIds[] = {
        JungleStretchAudioProcessor::intensityParamId,
        JungleStretchAudioProcessor::loopLengthParamId,
        JungleStretchAudioProcessor::chopRateParamId,
        JungleStretchAudioProcessor::mixParamId,
        JungleStretchAudioProcessor::triggerWindowParamId,
        JungleStretchAudioProcessor::triggerChanceParamId,
        JungleStretchAudioProcessor::manualBpmParamId,
        JungleStretchAudioProcessor::pitchParamId
    };
}

JungleStretchAudioProcessorEditor::JungleStretchAudioProcessorEditor (JungleStretchAudioProcessor& p)
    : AudioProcessorEditor (&p), processorRef (p), genericEditor (p)
{
    juce::StringArray presetNames;
    for (auto& preset : JungleStretchPresets::getFactoryPresets())
        presetNames.add (preset.name);
    presetToolbar.setPresetNames (presetNames);

    presetToolbar.onPresetSelected = [this] (int index) { selectPreset (index); };
    presetToolbar.onRandomise = [this]
    {
        JungleStretchPresets::randomise (processorRef.apvts);
        presetToolbar.showUnsavedLabel ("Random");
    };
    presetToolbar.isDirty = [this]
    {
        const auto& presets = JungleStretchPresets::getFactoryPresets();
        if (selectedPresetIndex < 0 || selectedPresetIndex >= (int) presets.size())
            return false;

        const auto current = JungleStretchPresets::captureCurrentValues (processorRef.apvts);
        return ! JungleStretchPresets::matches (current, presets[(size_t) selectedPresetIndex]);
    };

    addAndMakeVisible (presetToolbar);

    // A true first open (no restored DAW state) loads "Default" through the same
    // apply path a user picking it from the dropdown would use, so the plugin
    // never sits on raw parameter defaults that happen to look like a preset.
    if (! processorRef.hasRestoredState)
        selectPreset (0);
    presetToolbar.setSelectedPreset (0);

    addAndMakeVisible (genericEditor);

    for (auto* paramId : trackedParamIds)
        processorRef.apvts.addParameterListener (paramId, this);

    setSize (genericEditor.getWidth(), genericEditor.getHeight() + toolbarHeight);
}

JungleStretchAudioProcessorEditor::~JungleStretchAudioProcessorEditor()
{
    for (auto* paramId : trackedParamIds)
        processorRef.apvts.removeParameterListener (paramId, this);
}

void JungleStretchAudioProcessorEditor::selectPreset (int index)
{
    const auto& presets = JungleStretchPresets::getFactoryPresets();
    if (index < 0 || index >= (int) presets.size())
        return;

    selectedPresetIndex = index;
    JungleStretchPresets::apply (processorRef.apvts, presets[(size_t) index]);
}

void JungleStretchAudioProcessorEditor::parameterChanged (const juce::String&, float)
{
    triggerAsyncUpdate();
}

void JungleStretchAudioProcessorEditor::handleAsyncUpdate()
{
    presetToolbar.refreshDisplay();
}

void JungleStretchAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));
}

void JungleStretchAudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds();
    presetToolbar.setBounds (bounds.removeFromTop (toolbarHeight));
    genericEditor.setBounds (bounds);
}
