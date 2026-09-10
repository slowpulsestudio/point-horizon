#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

// Preset data and apply/capture/randomise helpers for JungleStretch, kept
// separate from the generic sps::PresetToolbar UI component (see
// Source/Components/PresetToolbar.h). Mode, Pitch Mode, and Singularity are
// deliberately excluded from all of this — they're global processing-mode
// toggles, not creative/tunable values (see "Preset-defining values vs.
// global mode toggles" in master-skills.md).
namespace JungleStretchPresets
{

struct Values
{
    juce::String name;
    float intensity;
    float loopLengthMs;
    float chopRate;
    float mix;
    float triggerWindow;
    float triggerChance;
    float manualBpm;
    float pitch;
};

// Only one factory preset exists so far — add more here as they're designed.
inline const std::vector<Values>& getFactoryPresets()
{
    static const std::vector<Values> presets {
        { "Default", 40.0f, 4000.0f, 50.0f, 100.0f, 25.0f, 100.0f, 120.0f, 0.0f }
    };
    return presets;
}

inline void apply (juce::AudioProcessorValueTreeState& apvts, const Values& values)
{
    auto setParam = [&apvts] (const char* paramId, float rawValue)
    {
        if (auto* param = apvts.getParameter (paramId))
            param->setValueNotifyingHost (param->convertTo0to1 (rawValue));
    };

    setParam (JungleStretchAudioProcessor::intensityParamId, values.intensity);
    setParam (JungleStretchAudioProcessor::loopLengthParamId, values.loopLengthMs);
    setParam (JungleStretchAudioProcessor::chopRateParamId, values.chopRate);
    setParam (JungleStretchAudioProcessor::mixParamId, values.mix);
    setParam (JungleStretchAudioProcessor::triggerWindowParamId, values.triggerWindow);
    setParam (JungleStretchAudioProcessor::triggerChanceParamId, values.triggerChance);
    setParam (JungleStretchAudioProcessor::manualBpmParamId, values.manualBpm);
    setParam (JungleStretchAudioProcessor::pitchParamId, values.pitch);
}

inline Values captureCurrentValues (juce::AudioProcessorValueTreeState& apvts)
{
    return {
        {},
        apvts.getRawParameterValue (JungleStretchAudioProcessor::intensityParamId)->load(),
        apvts.getRawParameterValue (JungleStretchAudioProcessor::loopLengthParamId)->load(),
        apvts.getRawParameterValue (JungleStretchAudioProcessor::chopRateParamId)->load(),
        apvts.getRawParameterValue (JungleStretchAudioProcessor::mixParamId)->load(),
        apvts.getRawParameterValue (JungleStretchAudioProcessor::triggerWindowParamId)->load(),
        apvts.getRawParameterValue (JungleStretchAudioProcessor::triggerChanceParamId)->load(),
        apvts.getRawParameterValue (JungleStretchAudioProcessor::manualBpmParamId)->load(),
        apvts.getRawParameterValue (JungleStretchAudioProcessor::pitchParamId)->load()
    };
}

inline bool matches (const Values& a, const Values& b, float epsilon = 0.001f)
{
    return std::abs (a.intensity - b.intensity) < epsilon
        && std::abs (a.loopLengthMs - b.loopLengthMs) < epsilon
        && std::abs (a.chopRate - b.chopRate) < epsilon
        && std::abs (a.mix - b.mix) < epsilon
        && std::abs (a.triggerWindow - b.triggerWindow) < epsilon
        && std::abs (a.triggerChance - b.triggerChance) < epsilon
        && std::abs (a.manualBpm - b.manualBpm) < epsilon
        && std::abs (a.pitch - b.pitch) < epsilon;
}

inline void randomise (juce::AudioProcessorValueTreeState& apvts)
{
    auto& random = juce::Random::getSystemRandom();

    auto randomiseParam = [&apvts, &random] (const char* paramId)
    {
        if (auto* param = apvts.getParameter (paramId))
            param->setValueNotifyingHost (random.nextFloat());
    };

    randomiseParam (JungleStretchAudioProcessor::intensityParamId);
    randomiseParam (JungleStretchAudioProcessor::loopLengthParamId);
    randomiseParam (JungleStretchAudioProcessor::chopRateParamId);
    randomiseParam (JungleStretchAudioProcessor::mixParamId);
    randomiseParam (JungleStretchAudioProcessor::triggerWindowParamId);
    randomiseParam (JungleStretchAudioProcessor::triggerChanceParamId);
    randomiseParam (JungleStretchAudioProcessor::manualBpmParamId);
    randomiseParam (JungleStretchAudioProcessor::pitchParamId);
}

} // namespace JungleStretchPresets
