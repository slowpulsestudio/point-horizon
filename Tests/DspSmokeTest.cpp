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
}

int main()
{
    bool allPassed = true;
    allPassed = testMixZeroIsBitExactBypass() && allPassed;
    allPassed = testStretchProducesFiniteWetAudio() && allPassed;
    allPassed = testDragModeIsStable() && allPassed;

    std::cout << (allPassed ? "ALL TESTS PASSED" : "SOME TESTS FAILED") << std::endl;
    return allPassed ? 0 : 1;
}
