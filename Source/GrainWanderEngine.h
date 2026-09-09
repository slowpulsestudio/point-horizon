#pragma once

#include <JuceHeader.h>

/**
 * Real-time port of the validated offline grain-index-wandering algorithm
 * (Prototyping/jungle_stretch_prototype.py's jungle_stretch()/apply_windowed()).
 *
 * Two deliberate adaptations from the offline whole-file version, both driven
 * by real-time/causality constraints (no plugin latency is introduced):
 *  - Stretch mode's grain pool is the last "Loop Length" ms of live audio (a
 *    rolling history ring buffer), not the whole file.
 *  - Drag mode's grain pool, for the active trailing window of a bar, is the
 *    *same* window position captured one bar earlier (already sitting in
 *    history), instead of the live not-yet-fully-arrived current window.
 */
class GrainWanderEngine
{
public:
    GrainWanderEngine() = default;

    enum class Mode { stretch = 0, drag = 1 };

    struct Parameters
    {
        Mode mode = Mode::stretch;
        float intensity01 = 0.4f;
        float loopLengthMs = 4000.0f;
        float chopRate01 = 0.5f;
        float mix01 = 1.0f;
        float triggerWindow01 = 0.25f;
        float triggerChance01 = 1.0f;
        double manualBpm = 120.0;
    };

    void prepare (double sampleRateIn, int maxBlockSize, int numChannelsIn);
    void reset();
    void process (juce::AudioBuffer<float>& buffer, const Parameters& params, juce::AudioPlayHead* playHead);

private:
    struct WanderState
    {
        double grainPos = 0.0;
        int runGrainsLeft = 0;
        double runSpeed = 1.0;

        void resetForNewWindow() noexcept
        {
            grainPos = 0.0;
            runGrainsLeft = 0;
            runSpeed = 1.0;
        }
    };

    double sampleRate = 44100.0;
    int numChannels = 2;

    juce::AudioBuffer<float> history;
    int historyCapacitySamples = 0;
    juce::int64 samplesWritten = 0;

    juce::AudioBuffer<float> dryScratch;
    juce::SmoothedValue<float> mixSmoothed;

    juce::Random random;

    // Stretch mode (continuous wander over the rolling history buffer).
    WanderState stretchWander;
    int stretchGrainFrames = 0;
    int stretchSamplesIntoGrain = 0;
    juce::int64 stretchCurrentSourceAbsStart = -1;

    // Drag mode (per-window wander, sourced from one bar earlier).
    WanderState dragWander;
    int dragGrainFrames = 0;
    int dragSamplesIntoGrain = 0;
    juce::int64 dragCurrentSourceAbsStart = -1;
    juce::int64 dragPoolAbsStart = 0;
    int dragPoolLenSamples = 0;
    bool dragWindowWasActive = false;
    juce::int64 lastBarIndexSeen = -1;
    bool currentBarPassesChance = true;
    juce::int64 fallbackTransportPos = 0;

    void writeToHistory (const juce::AudioBuffer<float>& input);
    void copyFromHistory (juce::AudioBuffer<float>& dest, int destStartSample,
                           juce::int64 absoluteSourceStart, int len) const;
    bool isAbsRangeAvailable (juce::int64 absStart, int len) const noexcept;

    juce::int64 pickNextSourceGrain (WanderState& state, juce::int64 poolAbsStart, int poolLenSamples,
                                      int grainFrames, int runLenMin, int runLenMax,
                                      double speedLo, double speedHi);

    void processStretch (juce::AudioBuffer<float>& buffer, const Parameters& params);
    void processDrag (juce::AudioBuffer<float>& buffer, const Parameters& params, juce::AudioPlayHead* playHead);

    static void computeIntensityParams (float intensity01, double& grainMs, double& speedLo, double& speedHi) noexcept;
    static void computeChopRange (float chopRate01, int& runLenMin, int& runLenMax) noexcept;
    int msToSamples (double ms) const noexcept;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GrainWanderEngine)
};
