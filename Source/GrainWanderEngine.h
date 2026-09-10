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
 *  - The windowed modes' (Drag, Stumble, Turnaround, Half-Time Drop) grain
 *    pool, for the active trailing window of a bar/beat, is the *same*
 *    window position captured one unit earlier (already sitting in history),
 *    instead of the live not-yet-fully-arrived current window.
 *
 * A live "rough pitching" control (turntable-style, not pitch-preserving) can
 * run in two modes: Whole Signal resamples the final mixed output from a
 * second rolling buffer (a small, unreported, variable delay only appears
 * while pitch is held off-centre); Grain Only resamples just the grain reads
 * used by Stretch/Drag, so untouched/dry audio stays at zero added latency.
 *
 * Singularity is a performance on/off toggle themed around real black-hole
 * mechanics; all five deepen together the longer it's held, via a shared
 * hold-time ramp (`singularityDepth01`) and a fast crossfade gate
 * (`singularityBlend`) that keeps engage/release click-free:
 *  - Gravity well: grain selection is pulled toward the pool's most recent
 *    grain, tightening the wander into a single loop.
 *  - Time dilation: the frozen loop's playback speed decays toward a
 *    near-static drone (Time Dilation Freeze; see applySingularity()).
 *  - Redshift: pitch auto-ramps toward -50% while held, snapping back to
 *    the user's raw Pitch knob the instant it's released.
 *  - Spaghettification: grain length itself stretches from short to very
 *    long the longer it's held.
 *  - Supernova: the final wet output is fed back into the history buffer,
 *    compounding into a denser, self-reinforcing texture until released.
 */
class GrainWanderEngine
{
public:
    GrainWanderEngine() = default;

    enum class Mode { stretch = 0, drag = 1, stumble = 2, turnaround = 3, halfTimeDrop = 4 };
    enum class PitchMode { wholeSignal = 0, grainOnly = 1 };

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
        float pitchPercent = 0.0f; // -50 .. +50, 0 = no pitch change
        PitchMode pitchMode = PitchMode::wholeSignal;
        bool singularityEngaged = false;
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

    // Second ring buffer, fed by the final dry/wet-mixed output — only used
    // for Whole Signal pitch mode, kept separate so Grain Only pitch mode
    // (and pitch-off playback) never pays for it.
    juce::AudioBuffer<float> pitchHistory;
    int pitchHistoryCapacitySamples = 0;
    juce::int64 pitchSamplesWritten = 0;
    double wholeSignalReadPos = 0.0;
    bool wholeSignalEngaged = false;

    // Freeze-and-slow performance effect, sourced straight from `history` so
    // it works identically regardless of Stretch/Drag mode.
    juce::SmoothedValue<float> singularityBlend;
    bool singularityWasEngaged = false;
    double singularityHoldSeconds = 0.0;
    juce::int64 singularityAnchorAbsStart = 0;
    double singularityLoopOffset = 0.0;
    int singularityWindowLenSamples = 0;

    // Shared "how deep in" ramp (0..1, grows with singularityHoldSeconds),
    // used by Gravity well / Spaghettification / Redshift / Supernova.
    double singularityDepth01 = 0.0;
    // Redshift: raw Pitch knob while released, ramped toward -50% while held.
    double currentEffectivePitchPercent = 0.0;
    // Supernova: the previous block's final wet output, fed back into history.
    juce::AudioBuffer<float> feedbackScratch;

    juce::AudioBuffer<float> dryScratch;
    juce::SmoothedValue<float> mixSmoothed;

    // Fixed seed: matches the offline Python prototype's validation habit of
    // using a deterministic RNG, so the wander character is reproducible run
    // to run for a given input (and, incidentally, keeps automated tests
    // comparing two engine instances deterministic too).
    juce::Random random { 1 };

    // Stretch mode (continuous wander over the rolling history buffer).
    WanderState stretchWander;
    int stretchGrainFrames = 0;
    int stretchSamplesIntoGrain = 0;
    juce::int64 stretchCurrentSourceAbsStart = -1;
    double stretchGrainReadPos = 0.0;

    // Windowed modes (Drag, Stumble, Turnaround, Half-Time Drop): all share
    // the same rhythm-gated-window scaffolding (wander confined to a trailing
    // fraction of a bar or beat, sourced from the same window position one
    // unit earlier), differing only in unit length / eligibility / speed
    // config (see WindowedConfig in processWindowed()). Only one windowed
    // mode can be active at a time, so they share this state.
    WanderState windowedWander;
    int windowedGrainFrames = 0;
    int windowedSamplesIntoGrain = 0;
    juce::int64 windowedCurrentSourceAbsStart = -1;
    juce::int64 windowedPoolAbsStart = 0;
    int windowedPoolLenSamples = 0;
    bool windowedWasActive = false;
    juce::int64 lastUnitIndexSeen = -1;
    bool currentUnitPassesChance = true;
    juce::int64 fallbackTransportPos = 0;
    double windowedGrainReadPos = 0.0;

    void writeToHistory (const juce::AudioBuffer<float>& input);
    void copyFromHistory (juce::AudioBuffer<float>& dest, int destStartSample,
                           juce::int64 absoluteSourceStart, int len) const;
    void copyFromHistoryPitched (juce::AudioBuffer<float>& dest, int destStartSample, int len,
                                  double& readPos, double ratio) const;
    bool isAbsRangeAvailable (juce::int64 absStart, int len) const noexcept;

    static void writeBlockToRing (juce::AudioBuffer<float>& ring, int capacity, juce::int64& samplesWrittenRef,
                                   const juce::AudioBuffer<float>& input, int numChannelsToWrite);
    static float readRingInterpolated (const juce::AudioBuffer<float>& ring, int capacity,
                                        int channel, double absoluteFractionalPos) noexcept;

    juce::int64 pickNextSourceGrain (WanderState& state, juce::int64 poolAbsStart, int poolLenSamples,
                                      int grainFrames, int runLenMin, int runLenMax,
                                      double speedLo, double speedHi, double gravityWellPull);

    // Per-mode config for the shared windowed-mode scaffolding.
    struct WindowedConfig
    {
        bool perBeat = false;        // Stumble: gate per-beat instead of per-bar.
        int everyNUnits = 1;         // Turnaround: only every Nth unit is eligible.
        float grainMsOverride = 0.0f; // Stumble: fixed grain size (0 = use Intensity mapping).
        bool freezeRun = false;      // Half-Time Drop: force a near-frozen, single sustained run.
    };

    static WindowedConfig windowedConfigFor (Mode mode) noexcept;

    void processStretch (juce::AudioBuffer<float>& buffer, const Parameters& params);
    void processWindowed (juce::AudioBuffer<float>& buffer, const Parameters& params, juce::AudioPlayHead* playHead,
                           const WindowedConfig& config);
    void applyWholeSignalPitch (juce::AudioBuffer<float>& buffer, float pitchPercent);
    void updateSingularityState (const Parameters& params, int numSamples);
    void applySingularity (juce::AudioBuffer<float>& buffer, const Parameters& params);

    static void computeIntensityParams (float intensity01, double& grainMs, double& speedLo, double& speedHi) noexcept;
    static void computeChopRange (float chopRate01, int& runLenMin, int& runLenMax) noexcept;
    static double pitchPercentToRatio (float pitchPercent) noexcept;
    static double blendSpaghettiGrainMs (double normalGrainMs, double depth01, double gate) noexcept;
    int msToSamples (double ms) const noexcept;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GrainWanderEngine)
};
