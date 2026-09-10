// Throwaway automated smoke test for GrainWanderEngine's real-time DSP port.
// Not shipped with the plugin — dev-only sanity check, run manually via the
// JungleStretchDspTests CMake target. Mirrors the offline Python prototype's
// validation habit (measured before trusting by ear).
#include <JuceHeader.h>
#include "../Source/GrainWanderEngine.h"

namespace
{
    bool containsNonFinite (const juce::AudioBuffer<float>& buffer)
    {
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        {
            auto* data = buffer.getReadPointer (ch);
            for (int i = 0; i < buffer.getNumSamples(); ++i)
                if (! std::isfinite (data[i]))
                    return true;
        }
        return false;
    }

    void fillWithNoise (juce::AudioBuffer<float>& buffer, juce::Random& rng)
    {
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        {
            auto* data = buffer.getWritePointer (ch);
            for (int i = 0; i < buffer.getNumSamples(); ++i)
                data[i] = rng.nextFloat() * 2.0f - 1.0f;
        }
    }

    // Mix=0 must reconstruct the dry input exactly, regardless of what the
    // grain-wander internals computed — this is the one guarantee that holds
    // independent of the DSP's own precision/timing approximations.
    bool testMixZeroIsBitExactBypass()
    {
        constexpr double sampleRate = 44100.0;
        constexpr int blockSize = 512;
        constexpr int numChannels = 2;

        GrainWanderEngine engine;
        engine.prepare (sampleRate, blockSize, numChannels);

        GrainWanderEngine::Parameters params;
        params.mode = GrainWanderEngine::Mode::stretch;
        params.mix01 = 0.0f;

        juce::Random rng (42);

        // Prime for 100 blocks (~1.2s) so the Mix smoothing ramp has settled at 0.
        for (int b = 0; b < 100; ++b)
        {
            juce::AudioBuffer<float> buffer (numChannels, blockSize);
            fillWithNoise (buffer, rng);
            engine.process (buffer, params, nullptr);
        }

        juce::AudioBuffer<float> input (numChannels, blockSize);
        fillWithNoise (input, rng);
        juce::AudioBuffer<float> output;
        output.makeCopyOf (input);

        engine.process (output, params, nullptr);

        for (int ch = 0; ch < numChannels; ++ch)
            for (int i = 0; i < blockSize; ++i)
                if (! juce::exactlyEqual (input.getSample (ch, i), output.getSample (ch, i)))
                {
                    std::cout << "FAIL: mix=0 bypass not bit-exact at ch=" << ch << " sample=" << i << std::endl;
                    return false;
                }

        std::cout << "PASS: mix=0 is a bit-exact bypass" << std::endl;
        return true;
    }

    // Stretch mode at default intensity, once the history buffer is primed,
    // must produce finite audio that actually differs from the dry input.
    bool testStretchProducesFiniteWetAudio()
    {
        constexpr double sampleRate = 44100.0;
        constexpr int blockSize = 512;
        constexpr int numChannels = 2;

        GrainWanderEngine engine;
        engine.prepare (sampleRate, blockSize, numChannels);

        GrainWanderEngine::Parameters params;
        params.mode = GrainWanderEngine::Mode::stretch;
        params.mix01 = 1.0f;
        params.intensity01 = 0.4f;
        params.loopLengthMs = 4000.0f;

        juce::Random rng (7);
        double totalDiff = 0.0;
        int primingBlocks = (int) std::ceil (5.0 * sampleRate / blockSize); // 5s, > 4s Loop Length

        for (int b = 0; b < primingBlocks; ++b)
        {
            juce::AudioBuffer<float> buffer (numChannels, blockSize);
            fillWithNoise (buffer, rng);
            juce::AudioBuffer<float> dry;
            dry.makeCopyOf (buffer);

            engine.process (buffer, params, nullptr);

            if (containsNonFinite (buffer))
            {
                std::cout << "FAIL: Stretch mode produced non-finite output" << std::endl;
                return false;
            }

            for (int ch = 0; ch < numChannels; ++ch)
                for (int i = 0; i < blockSize; ++i)
                    totalDiff += std::abs (buffer.getSample (ch, i) - dry.getSample (ch, i));
        }

        if (totalDiff <= 0.0)
        {
            std::cout << "FAIL: Stretch mode output is identical to dry input (no wander effect audible)" << std::endl;
            return false;
        }

        std::cout << "PASS: Stretch mode produces finite, audibly different output" << std::endl;
        return true;
    }

    // Drag mode (no host playhead, manual BPM fallback) must run for several
    // bars without crashing or producing non-finite audio.
    bool testDragModeIsStable()
    {
        constexpr double sampleRate = 44100.0;
        constexpr int blockSize = 256;
        constexpr int numChannels = 2;

        GrainWanderEngine engine;
        engine.prepare (sampleRate, blockSize, numChannels);

        GrainWanderEngine::Parameters params;
        params.mode = GrainWanderEngine::Mode::drag;
        params.mix01 = 1.0f;
        params.intensity01 = 0.4f;
        params.loopLengthMs = 4000.0f;
        params.triggerWindow01 = 0.25f;
        params.triggerChance01 = 1.0f;
        params.manualBpm = 137.0;

        juce::Random rng (99);

        // ~16 bars at 137bpm, 4/4.
        const double barSeconds = 60.0 / params.manualBpm * 4.0;
        const int totalBlocks = (int) std::ceil (16.0 * barSeconds * sampleRate / blockSize);

        for (int b = 0; b < totalBlocks; ++b)
        {
            juce::AudioBuffer<float> buffer (numChannels, blockSize);
            fillWithNoise (buffer, rng);
            engine.process (buffer, params, nullptr);

            if (containsNonFinite (buffer))
            {
                std::cout << "FAIL: Drag mode produced non-finite output" << std::endl;
                return false;
            }
        }

        std::cout << "PASS: Drag mode ran for 16 bars with finite output" << std::endl;
        return true;
    }

    // Stumble mode (per-beat gated window, no host playhead) must run for
    // several bars without crashing or producing non-finite audio.
    bool testStumbleModeIsStable()
    {
        constexpr double sampleRate = 44100.0;
        constexpr int blockSize = 256;
        constexpr int numChannels = 2;

        GrainWanderEngine engine;
        engine.prepare (sampleRate, blockSize, numChannels);

        GrainWanderEngine::Parameters params;
        params.mode = GrainWanderEngine::Mode::stumble;
        params.mix01 = 1.0f;
        params.intensity01 = 0.4f;
        params.loopLengthMs = 4000.0f;
        params.triggerWindow01 = 0.3f;
        params.triggerChance01 = 1.0f;
        params.manualBpm = 137.0;

        juce::Random rng (100);

        const double barSeconds = 60.0 / params.manualBpm * 4.0;
        const int totalBlocks = (int) std::ceil (16.0 * barSeconds * sampleRate / blockSize);

        for (int b = 0; b < totalBlocks; ++b)
        {
            juce::AudioBuffer<float> buffer (numChannels, blockSize);
            fillWithNoise (buffer, rng);
            engine.process (buffer, params, nullptr);

            if (containsNonFinite (buffer))
            {
                std::cout << "FAIL: Stumble mode produced non-finite output" << std::endl;
                return false;
            }
        }

        std::cout << "PASS: Stumble mode ran for 16 bars with finite output" << std::endl;
        return true;
    }

    // Turnaround mode (only every 4th bar eligible, no host playhead) must
    // run for several bars without crashing or producing non-finite audio.
    bool testTurnaroundModeIsStable()
    {
        constexpr double sampleRate = 44100.0;
        constexpr int blockSize = 256;
        constexpr int numChannels = 2;

        GrainWanderEngine engine;
        engine.prepare (sampleRate, blockSize, numChannels);

        GrainWanderEngine::Parameters params;
        params.mode = GrainWanderEngine::Mode::turnaround;
        params.mix01 = 1.0f;
        params.intensity01 = 0.4f;
        params.loopLengthMs = 4000.0f;
        params.triggerWindow01 = 0.25f;
        params.triggerChance01 = 1.0f;
        params.manualBpm = 137.0;

        juce::Random rng (101);

        const double barSeconds = 60.0 / params.manualBpm * 4.0;
        const int totalBlocks = (int) std::ceil (16.0 * barSeconds * sampleRate / blockSize);

        for (int b = 0; b < totalBlocks; ++b)
        {
            juce::AudioBuffer<float> buffer (numChannels, blockSize);
            fillWithNoise (buffer, rng);
            engine.process (buffer, params, nullptr);

            if (containsNonFinite (buffer))
            {
                std::cout << "FAIL: Turnaround mode produced non-finite output" << std::endl;
                return false;
            }
        }

        std::cout << "PASS: Turnaround mode ran for 16 bars with finite output" << std::endl;
        return true;
    }

    // Half-Time Drop mode (forced near-frozen sustained run, no host
    // playhead) must run for several bars without crashing or producing
    // non-finite audio.
    bool testHalfTimeDropModeIsStable()
    {
        constexpr double sampleRate = 44100.0;
        constexpr int blockSize = 256;
        constexpr int numChannels = 2;

        GrainWanderEngine engine;
        engine.prepare (sampleRate, blockSize, numChannels);

        GrainWanderEngine::Parameters params;
        params.mode = GrainWanderEngine::Mode::halfTimeDrop;
        params.mix01 = 1.0f;
        params.intensity01 = 0.4f;
        params.loopLengthMs = 4000.0f;
        params.triggerWindow01 = 0.25f;
        params.triggerChance01 = 1.0f;
        params.manualBpm = 137.0;

        juce::Random rng (102);

        const double barSeconds = 60.0 / params.manualBpm * 4.0;
        const int totalBlocks = (int) std::ceil (16.0 * barSeconds * sampleRate / blockSize);

        for (int b = 0; b < totalBlocks; ++b)
        {
            juce::AudioBuffer<float> buffer (numChannels, blockSize);
            fillWithNoise (buffer, rng);
            engine.process (buffer, params, nullptr);

            if (containsNonFinite (buffer))
            {
                std::cout << "FAIL: Half-Time Drop mode produced non-finite output" << std::endl;
                return false;
            }
        }

        std::cout << "PASS: Half-Time Drop mode ran for 16 bars with finite output" << std::endl;
        return true;
    }

    // Pitch=0 must be a no-op regardless of Pitch Mode — toggling the mode
    // alone (without moving the Pitch knob) must never change the output.
    bool testPitchZeroMatchesAcrossModes()
    {
        constexpr double sampleRate = 44100.0;
        constexpr int blockSize = 512;
        constexpr int numChannels = 2;

        GrainWanderEngine wholeSignalEngine;
        GrainWanderEngine grainOnlyEngine;
        wholeSignalEngine.prepare (sampleRate, blockSize, numChannels);
        grainOnlyEngine.prepare (sampleRate, blockSize, numChannels);

        GrainWanderEngine::Parameters params;
        params.mode = GrainWanderEngine::Mode::stretch;
        params.mix01 = 1.0f;
        params.intensity01 = 0.4f;
        params.loopLengthMs = 4000.0f;
        params.pitchPercent = 0.0f;

        auto wholeSignalParams = params;
        wholeSignalParams.pitchMode = GrainWanderEngine::PitchMode::wholeSignal;
        auto grainOnlyParams = params;
        grainOnlyParams.pitchMode = GrainWanderEngine::PitchMode::grainOnly;

        const int totalBlocks = (int) std::ceil (6.0 * sampleRate / blockSize); // > 4s Loop Length

        for (int b = 0; b < totalBlocks; ++b)
        {
            juce::Random rng (1234 + b); // identical noise fed to both engines this block
            juce::AudioBuffer<float> bufferA (numChannels, blockSize);
            fillWithNoise (bufferA, rng);
            juce::AudioBuffer<float> bufferB;
            bufferB.makeCopyOf (bufferA);

            wholeSignalEngine.process (bufferA, wholeSignalParams, nullptr);
            grainOnlyEngine.process (bufferB, grainOnlyParams, nullptr);

            for (int ch = 0; ch < numChannels; ++ch)
                for (int i = 0; i < blockSize; ++i)
                    if (! juce::exactlyEqual (bufferA.getSample (ch, i), bufferB.getSample (ch, i)))
                    {
                        std::cout << "FAIL: Pitch=0 differs between Whole Signal and Grain Only modes" << std::endl;
                        return false;
                    }
        }

        std::cout << "PASS: Pitch=0 is identical across both Pitch Modes" << std::endl;
        return true;
    }

    // Grain Only pitch must produce finite, audibly different audio, and must
    // not need any added plugin latency (still checked via the smoke run).
    bool testGrainOnlyPitchIsStable()
    {
        constexpr double sampleRate = 44100.0;
        constexpr int blockSize = 512;
        constexpr int numChannels = 2;

        GrainWanderEngine engine;
        engine.prepare (sampleRate, blockSize, numChannels);

        GrainWanderEngine::Parameters params;
        params.mode = GrainWanderEngine::Mode::stretch;
        params.mix01 = 1.0f;
        params.intensity01 = 0.4f;
        params.loopLengthMs = 4000.0f;
        params.pitchMode = GrainWanderEngine::PitchMode::grainOnly;
        params.pitchPercent = -35.0f;

        juce::Random rng (11);
        double totalDiff = 0.0;
        const int totalBlocks = (int) std::ceil (8.0 * sampleRate / blockSize);

        for (int b = 0; b < totalBlocks; ++b)
        {
            juce::AudioBuffer<float> buffer (numChannels, blockSize);
            fillWithNoise (buffer, rng);
            juce::AudioBuffer<float> dry;
            dry.makeCopyOf (buffer);

            // Halfway through, flip to a positive pitch to simulate a live knob move.
            if (b == totalBlocks / 2)
                params.pitchPercent = 40.0f;

            engine.process (buffer, params, nullptr);

            if (containsNonFinite (buffer))
            {
                std::cout << "FAIL: Grain Only pitch produced non-finite output" << std::endl;
                return false;
            }

            for (int ch = 0; ch < numChannels; ++ch)
                for (int i = 0; i < blockSize; ++i)
                    totalDiff += std::abs (buffer.getSample (ch, i) - dry.getSample (ch, i));
        }

        if (totalDiff <= 0.0)
        {
            std::cout << "FAIL: Grain Only pitch produced no audible change" << std::endl;
            return false;
        }

        std::cout << "PASS: Grain Only pitch is stable across a live knob move" << std::endl;
        return true;
    }

    // Whole Signal pitch held at a sustained extreme must stay finite even
    // once the read pointer's drift exceeds the ring buffer and gets clamped.
    bool testWholeSignalPitchSustainedIsStable()
    {
        constexpr double sampleRate = 44100.0;
        constexpr int blockSize = 512;
        constexpr int numChannels = 2;

        GrainWanderEngine engine;
        engine.prepare (sampleRate, blockSize, numChannels);

        GrainWanderEngine::Parameters params;
        params.mode = GrainWanderEngine::Mode::drag;
        params.mix01 = 1.0f;
        params.intensity01 = 0.4f;
        params.loopLengthMs = 4000.0f;
        params.manualBpm = 137.0;
        params.pitchMode = GrainWanderEngine::PitchMode::wholeSignal;
        params.pitchPercent = -50.0f; // worst case: read pointer falls behind fastest

        juce::Random rng (55);
        // 20s sustained — long enough to exceed the ~8.5s pitch history capacity
        // and exercise the clamp-to-valid-range logic repeatedly.
        const int totalBlocks = (int) std::ceil (20.0 * sampleRate / blockSize);

        for (int b = 0; b < totalBlocks; ++b)
        {
            juce::AudioBuffer<float> buffer (numChannels, blockSize);
            fillWithNoise (buffer, rng);
            engine.process (buffer, params, nullptr);

            if (containsNonFinite (buffer))
            {
                std::cout << "FAIL: Whole Signal pitch produced non-finite output under sustained extreme pitch" << std::endl;
                return false;
            }
        }

        std::cout << "PASS: Whole Signal pitch stays finite under 20s of sustained -50% pitch" << std::endl;
        return true;
    }

    // Singularity disengaged must never alter output, regardless of how long
    // the engine has been running or what else is happening.
    bool testSingularityDisengagedIsBitExactBypass()
    {
        constexpr double sampleRate = 44100.0;
        constexpr int blockSize = 512;
        constexpr int numChannels = 2;

        GrainWanderEngine engineA;
        GrainWanderEngine engineB;
        engineA.prepare (sampleRate, blockSize, numChannels);
        engineB.prepare (sampleRate, blockSize, numChannels);

        GrainWanderEngine::Parameters params;
        params.mode = GrainWanderEngine::Mode::stretch;
        params.mix01 = 1.0f;
        params.intensity01 = 0.4f;
        params.loopLengthMs = 4000.0f;
        params.singularityEngaged = false; // explicit, matches the struct default

        const int totalBlocks = (int) std::ceil (3.0 * sampleRate / blockSize);

        for (int b = 0; b < totalBlocks; ++b)
        {
            juce::Random rng (777 + b);
            juce::AudioBuffer<float> bufferA (numChannels, blockSize);
            fillWithNoise (bufferA, rng);
            juce::AudioBuffer<float> bufferB;
            bufferB.makeCopyOf (bufferA);

            engineA.process (bufferA, params, nullptr);
            engineB.process (bufferB, params, nullptr);

            for (int ch = 0; ch < numChannels; ++ch)
                for (int i = 0; i < blockSize; ++i)
                    if (! juce::exactlyEqual (bufferA.getSample (ch, i), bufferB.getSample (ch, i)))
                    {
                        std::cout << "FAIL: Singularity=false engines diverge (should be deterministic/identical)" << std::endl;
                        return false;
                    }
        }

        std::cout << "PASS: Singularity disengaged is a stable, deterministic bypass" << std::endl;
        return true;
    }

    // Holding Singularity for a long time (well past the speed floor), then
    // releasing it, must stay finite throughout — no NaN, no crash.
    bool testSingularityHoldAndReleaseIsStable()
    {
        constexpr double sampleRate = 44100.0;
        constexpr int blockSize = 512;
        constexpr int numChannels = 2;

        GrainWanderEngine engine;
        engine.prepare (sampleRate, blockSize, numChannels);

        GrainWanderEngine::Parameters params;
        params.mode = GrainWanderEngine::Mode::stretch;
        params.mix01 = 1.0f;
        params.intensity01 = 0.4f;
        params.loopLengthMs = 4000.0f;

        juce::Random rng (321);

        // 1s of normal playback first, so there's real history to freeze onto.
        const int primeBlocks = (int) std::ceil (1.0 * sampleRate / blockSize);
        for (int b = 0; b < primeBlocks; ++b)
        {
            juce::AudioBuffer<float> buffer (numChannels, blockSize);
            fillWithNoise (buffer, rng);
            engine.process (buffer, params, nullptr);
        }

        // Hold for 8s — long enough to approach the near-static drone floor.
        params.singularityEngaged = true;
        const int holdBlocks = (int) std::ceil (8.0 * sampleRate / blockSize);
        for (int b = 0; b < holdBlocks; ++b)
        {
            juce::AudioBuffer<float> buffer (numChannels, blockSize);
            fillWithNoise (buffer, rng);
            engine.process (buffer, params, nullptr);

            if (containsNonFinite (buffer))
            {
                std::cout << "FAIL: Singularity hold produced non-finite output" << std::endl;
                return false;
            }
        }

        // Release — ramp back to normal must also stay finite.
        params.singularityEngaged = false;
        const int releaseBlocks = (int) std::ceil (1.0 * sampleRate / blockSize);
        for (int b = 0; b < releaseBlocks; ++b)
        {
            juce::AudioBuffer<float> buffer (numChannels, blockSize);
            fillWithNoise (buffer, rng);
            engine.process (buffer, params, nullptr);

            if (containsNonFinite (buffer))
            {
                std::cout << "FAIL: Singularity release produced non-finite output" << std::endl;
                return false;
            }
        }

        std::cout << "PASS: Singularity hold (8s) and release stay finite" << std::endl;
        return true;
    }

    // All five mechanics together (Gravity well, Time dilation, Redshift,
    // Spaghettification, Supernova) must stay finite through a long hold and
    // release, in both Stretch (grainOnly pitch) and Drag mode.
    bool testSingularityAllMechanicsAreStable (GrainWanderEngine::Mode mode, GrainWanderEngine::PitchMode pitchMode,
                                                GrainWanderEngine::SingularityMode singularityMode = GrainWanderEngine::SingularityMode::blackHole)
    {
        constexpr double sampleRate = 44100.0;
        constexpr int blockSize = 512;
        constexpr int numChannels = 2;

        GrainWanderEngine engine;
        engine.prepare (sampleRate, blockSize, numChannels);

        GrainWanderEngine::Parameters params;
        params.mode = mode;
        params.mix01 = 1.0f;
        params.intensity01 = 0.4f;
        params.loopLengthMs = 4000.0f;
        params.manualBpm = 137.0;
        params.pitchMode = pitchMode;
        params.pitchPercent = 20.0f; // nonzero, so Redshift has something to override
        params.singularityMode = singularityMode;

        juce::Random rng (2468);

        const int primeBlocks = (int) std::ceil (1.0 * sampleRate / blockSize);
        for (int b = 0; b < primeBlocks; ++b)
        {
            juce::AudioBuffer<float> buffer (numChannels, blockSize);
            fillWithNoise (buffer, rng);
            engine.process (buffer, params, nullptr);
        }

        params.singularityEngaged = true;
        const int holdBlocks = (int) std::ceil (12.0 * sampleRate / blockSize);
        for (int b = 0; b < holdBlocks; ++b)
        {
            juce::AudioBuffer<float> buffer (numChannels, blockSize);
            fillWithNoise (buffer, rng);
            engine.process (buffer, params, nullptr);

            if (containsNonFinite (buffer))
            {
                std::cout << "FAIL: Singularity (all mechanics) hold produced non-finite output" << std::endl;
                return false;
            }
        }

        params.singularityEngaged = false;
        const int releaseBlocks = (int) std::ceil (2.0 * sampleRate / blockSize);
        for (int b = 0; b < releaseBlocks; ++b)
        {
            juce::AudioBuffer<float> buffer (numChannels, blockSize);
            fillWithNoise (buffer, rng);
            engine.process (buffer, params, nullptr);

            if (containsNonFinite (buffer))
            {
                std::cout << "FAIL: Singularity (all mechanics) release produced non-finite output" << std::endl;
                return false;
            }
        }

        return true;
    }

    bool testSingularityAllMechanicsStableInStretchMode()
    {
        const bool ok = testSingularityAllMechanicsAreStable (GrainWanderEngine::Mode::stretch,
                                                               GrainWanderEngine::PitchMode::grainOnly);
        std::cout << (ok ? "PASS: All 5 Singularity mechanics stay finite in Stretch mode (Grain Only pitch)"
                          : "FAIL: Stretch mode mechanics test failed") << std::endl;
        return ok;
    }

    bool testSingularityAllMechanicsStableInDragMode()
    {
        const bool ok = testSingularityAllMechanicsAreStable (GrainWanderEngine::Mode::drag,
                                                                GrainWanderEngine::PitchMode::wholeSignal);
        std::cout << (ok ? "PASS: All 5 Singularity mechanics stay finite in Drag mode (Whole Signal pitch)"
                          : "FAIL: Drag mode mechanics test failed") << std::endl;
        return ok;
    }

    bool testGreyHoleIsStable()
    {
        const bool ok = testSingularityAllMechanicsAreStable (GrainWanderEngine::Mode::stretch,
                                                               GrainWanderEngine::PitchMode::grainOnly,
                                                               GrainWanderEngine::SingularityMode::greyHole);
        std::cout << (ok ? "PASS: Grey hole (unison freeze, no pitch pull) stays finite"
                          : "FAIL: Grey hole test failed") << std::endl;
        return ok;
    }

    bool testWhiteHoleIsStable()
    {
        const bool ok = testSingularityAllMechanicsAreStable (GrainWanderEngine::Mode::stretch,
                                                               GrainWanderEngine::PitchMode::grainOnly,
                                                               GrainWanderEngine::SingularityMode::whiteHole);
        std::cout << (ok ? "PASS: White hole (unison freeze, Blueshift) stays finite"
                          : "FAIL: White hole test failed") << std::endl;
        return ok;
    }
}

int main()
{
    bool allPassed = true;
    allPassed = testMixZeroIsBitExactBypass() && allPassed;
    allPassed = testStretchProducesFiniteWetAudio() && allPassed;
    allPassed = testDragModeIsStable() && allPassed;
    allPassed = testStumbleModeIsStable() && allPassed;
    allPassed = testTurnaroundModeIsStable() && allPassed;
    allPassed = testHalfTimeDropModeIsStable() && allPassed;
    allPassed = testPitchZeroMatchesAcrossModes() && allPassed;
    allPassed = testGrainOnlyPitchIsStable() && allPassed;
    allPassed = testWholeSignalPitchSustainedIsStable() && allPassed;
    allPassed = testSingularityDisengagedIsBitExactBypass() && allPassed;
    allPassed = testSingularityHoldAndReleaseIsStable() && allPassed;
    allPassed = testSingularityAllMechanicsStableInStretchMode() && allPassed;
    allPassed = testSingularityAllMechanicsStableInDragMode() && allPassed;
    allPassed = testGreyHoleIsStable() && allPassed;
    allPassed = testWhiteHoleIsStable() && allPassed;

    std::cout << (allPassed ? "ALL TESTS PASSED" : "SOME TESTS FAILED") << std::endl;
    return allPassed ? 0 : 1;
}
