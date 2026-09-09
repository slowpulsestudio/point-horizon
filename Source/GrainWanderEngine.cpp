#include "GrainWanderEngine.h"

void GrainWanderEngine::prepare (double sampleRateIn, int maxBlockSize, int numChannelsIn)
{
    sampleRate = sampleRateIn;
    numChannels = numChannelsIn;

    // Big enough for the max Loop Length (8s) plus headroom, at this sample rate.
    historyCapacitySamples = (int) std::ceil (8.5 * sampleRate);
    history.setSize (numChannels, historyCapacitySamples);
    history.clear();

    // Only used by Whole Signal pitch mode; same generous size for headroom
    // against sustained pitch-bend gestures.
    pitchHistoryCapacitySamples = historyCapacitySamples;
    pitchHistory.setSize (numChannels, pitchHistoryCapacitySamples);
    pitchHistory.clear();

    dryScratch.setSize (numChannels, maxBlockSize);
    mixSmoothed.reset (sampleRate, 0.02);

    singularityBlend.reset (sampleRate, 0.06);
    singularityWindowLenSamples = msToSamples (90.0);

    reset();
}

void GrainWanderEngine::reset()
{
    history.clear();
    samplesWritten = 0;

    stretchWander.resetForNewWindow();
    stretchGrainFrames = 0;
    stretchSamplesIntoGrain = 0;
    stretchCurrentSourceAbsStart = -1;

    dragWander.resetForNewWindow();
    dragGrainFrames = 0;
    dragSamplesIntoGrain = 0;
    dragCurrentSourceAbsStart = -1;
    dragPoolAbsStart = 0;
    dragPoolLenSamples = 0;
    dragWindowWasActive = false;
    lastBarIndexSeen = -1;
    currentBarPassesChance = true;
    fallbackTransportPos = 0;
    dragGrainReadPos = 0.0;

    stretchGrainReadPos = 0.0;

    pitchHistory.clear();
    pitchSamplesWritten = 0;
    wholeSignalReadPos = 0.0;
    wholeSignalEngaged = false;

    singularityWasEngaged = false;
    singularityHoldSeconds = 0.0;
    singularityAnchorAbsStart = 0;
    singularityLoopOffset = 0.0;
    singularityBlend.setCurrentAndTargetValue (0.0f);

    mixSmoothed.setCurrentAndTargetValue (1.0f);
}

double GrainWanderEngine::pitchPercentToRatio (float pitchPercent) noexcept
{
    // -50% .. +50% -> playback/read rate 0.5x .. 1.5x (turntable-pitch style).
    return 1.0 + (double) pitchPercent / 100.0;
}

void GrainWanderEngine::computeIntensityParams (float intensity01, double& grainMs, double& speedLo, double& speedHi) noexcept
{
    // Mirrors Prototyping/jungle_stretch_prototype.py's intensity_to_params().
    grainMs = 70.0 - (double) intensity01 * 50.0;
    const double intensityScaled = (double) intensity01 * 2.0;
    speedLo = juce::jmax (0.25, 1.0 - intensityScaled * 0.6);
    speedHi = 1.0 + intensityScaled * 1.5;
}

void GrainWanderEngine::computeChopRange (float chopRate01, int& runLenMin, int& runLenMax) noexcept
{
    // Chop Rate scales the validated run_len_range around the 50%-baseline
    // (3,10) tested offline: 0% -> long/smooth runs, 100% -> short/chattery runs.
    double lo, hi;
    if (chopRate01 <= 0.5f)
    {
        const double t = (double) chopRate01 / 0.5;
        lo = juce::jmap (t, 0.0, 1.0, 20.0, 3.0);
        hi = juce::jmap (t, 0.0, 1.0, 40.0, 10.0);
    }
    else
    {
        const double t = ((double) chopRate01 - 0.5) / 0.5;
        lo = juce::jmap (t, 0.0, 1.0, 3.0, 1.0);
        hi = juce::jmap (t, 0.0, 1.0, 10.0, 4.0);
    }

    runLenMin = juce::jmax (1, (int) std::round (lo));
    runLenMax = juce::jmax (runLenMin + 1, (int) std::round (hi));
}

int GrainWanderEngine::msToSamples (double ms) const noexcept
{
    return juce::jmax (1, (int) std::round (ms / 1000.0 * sampleRate));
}

void GrainWanderEngine::writeBlockToRing (juce::AudioBuffer<float>& ring, int capacity, juce::int64& samplesWrittenRef,
                                           const juce::AudioBuffer<float>& input, int numChannelsToWrite)
{
    const int numSamples = input.getNumSamples();
    const int physicalStart = (int) (samplesWrittenRef % (juce::int64) capacity);

    for (int ch = 0; ch < numChannelsToWrite; ++ch)
    {
        auto* dest = ring.getWritePointer (ch);
        auto* src = input.getReadPointer (juce::jmin (ch, input.getNumChannels() - 1));

        const int firstPart = juce::jmin (numSamples, capacity - physicalStart);
        juce::FloatVectorOperations::copy (dest + physicalStart, src, firstPart);

        const int remaining = numSamples - firstPart;
        if (remaining > 0)
            juce::FloatVectorOperations::copy (dest, src + firstPart, remaining);
    }

    samplesWrittenRef += numSamples;
}

float GrainWanderEngine::readRingInterpolated (const juce::AudioBuffer<float>& ring, int capacity,
                                                int channel, double absoluteFractionalPos) noexcept
{
    const double flo = std::floor (absoluteFractionalPos);
    const auto i0 = (juce::int64) flo;
    const double frac = absoluteFractionalPos - flo;

    auto wrap = [capacity] (juce::int64 idx) -> int
    {
        auto m = idx % (juce::int64) capacity;
        if (m < 0) m += capacity;
        return (int) m;
    };

    const auto p0 = wrap (i0);
    const auto p1 = wrap (i0 + 1);
    const auto* data = ring.getReadPointer (channel);
    return (float) (data[p0] + (data[p1] - data[p0]) * frac);
}

void GrainWanderEngine::writeToHistory (const juce::AudioBuffer<float>& input)
{
    writeBlockToRing (history, historyCapacitySamples, samplesWritten, input, numChannels);
}

void GrainWanderEngine::copyFromHistory (juce::AudioBuffer<float>& dest, int destStartSample,
                                          juce::int64 absoluteSourceStart, int len) const
{
    const int capacity = historyCapacitySamples;
    const auto physicalStart = (int) (((absoluteSourceStart % capacity) + capacity) % capacity);

    for (int ch = 0; ch < numChannels && ch < dest.getNumChannels(); ++ch)
    {
        auto* out = dest.getWritePointer (ch) + destStartSample;
        auto* src = history.getReadPointer (ch);

        const int firstPart = juce::jmin (len, capacity - physicalStart);
        juce::FloatVectorOperations::copy (out, src + physicalStart, firstPart);

        const int remaining = len - firstPart;
        if (remaining > 0)
            juce::FloatVectorOperations::copy (out + firstPart, src, remaining);
    }
}

bool GrainWanderEngine::isAbsRangeAvailable (juce::int64 absStart, int len) const noexcept
{
    if (absStart < 0 || len <= 0)
        return false;

    if (absStart + len > samplesWritten)
        return false; // would read not-yet-written samples

    return (samplesWritten - absStart) <= (juce::int64) historyCapacitySamples;
}

void GrainWanderEngine::copyFromHistoryPitched (juce::AudioBuffer<float>& dest, int destStartSample, int len,
                                                 double& readPos, double ratio) const
{
    const auto minAbs = juce::jmax ((juce::int64) 0, samplesWritten - historyCapacitySamples);
    const auto maxAbs = samplesWritten - 1;

    for (int i = 0; i < len; ++i)
    {
        const auto flooredAbs = (juce::int64) std::floor (readPos);
        double clampedPos = readPos;
        if (flooredAbs < minAbs)
            clampedPos = (double) minAbs;
        else if (flooredAbs + 1 > maxAbs)
            clampedPos = (double) juce::jmax (minAbs, maxAbs - 1);

        for (int ch = 0; ch < numChannels && ch < dest.getNumChannels(); ++ch)
            dest.setSample (ch, destStartSample + i, readRingInterpolated (history, historyCapacitySamples, ch, clampedPos));

        readPos += ratio;
    }
}

juce::int64 GrainWanderEngine::pickNextSourceGrain (WanderState& state, juce::int64 poolAbsStart, int poolLenSamples,
                                                     int grainFrames, int runLenMin, int runLenMax,
                                                     double speedLo, double speedHi)
{
    const int numGrains = poolLenSamples / grainFrames;
    if (numGrains < 2)
        return -1;

    if (state.runGrainsLeft <= 0)
    {
        state.runGrainsLeft = runLenMin + random.nextInt (juce::jmax (1, runLenMax - runLenMin));
        state.runSpeed = speedLo + random.nextDouble() * (speedHi - speedLo);
    }

    const auto srcIdx = ((juce::int64) state.grainPos) % (juce::int64) numGrains;
    state.grainPos += state.runSpeed;
    state.runGrainsLeft -= 1;

    return poolAbsStart + srcIdx * (juce::int64) grainFrames;
}

void GrainWanderEngine::processStretch (juce::AudioBuffer<float>& buffer, const Parameters& params)
{
    const int numSamples = buffer.getNumSamples();
    const bool pitchThisPath = params.pitchMode == PitchMode::grainOnly && params.pitchPercent != 0.0f;
    const double pitchRatio = pitchThisPath ? pitchPercentToRatio (params.pitchPercent) : 1.0;
    int pos = 0;

    while (pos < numSamples)
    {
        if (stretchSamplesIntoGrain >= stretchGrainFrames || stretchGrainFrames <= 0)
        {
            double grainMs, speedLo, speedHi;
            computeIntensityParams (params.intensity01, grainMs, speedLo, speedHi);
            stretchGrainFrames = msToSamples (grainMs);

            int runLenMin, runLenMax;
            computeChopRange (params.chopRate01, runLenMin, runLenMax);

            const int historyLenSamples = juce::jlimit (stretchGrainFrames * 2, historyCapacitySamples,
                                                          msToSamples ((double) params.loopLengthMs));
            const auto poolAbsStart = juce::jmax ((juce::int64) 0, samplesWritten - historyLenSamples);
            const auto poolLen = (int) (samplesWritten - poolAbsStart);

            stretchCurrentSourceAbsStart = pickNextSourceGrain (stretchWander, poolAbsStart, poolLen,
                                                                 stretchGrainFrames, runLenMin, runLenMax,
                                                                 speedLo, speedHi);
            stretchGrainReadPos = (double) stretchCurrentSourceAbsStart;
            stretchSamplesIntoGrain = 0;
        }

        const int chunk = juce::jmin (numSamples - pos, stretchGrainFrames - stretchSamplesIntoGrain);

        if (stretchCurrentSourceAbsStart >= 0)
        {
            if (pitchThisPath)
            {
                copyFromHistoryPitched (buffer, pos, chunk, stretchGrainReadPos, pitchRatio);
            }
            else
            {
                const auto srcStart = stretchCurrentSourceAbsStart + stretchSamplesIntoGrain;
                if (isAbsRangeAvailable (srcStart, chunk))
                    copyFromHistory (buffer, pos, srcStart, chunk);
            }
        }

        pos += chunk;
        stretchSamplesIntoGrain += chunk;
    }
}

void GrainWanderEngine::processDrag (juce::AudioBuffer<float>& buffer, const Parameters& params, juce::AudioPlayHead* playHead)
{
    const int numSamples = buffer.getNumSamples();
    const bool pitchThisPath = params.pitchMode == PitchMode::grainOnly && params.pitchPercent != 0.0f;
    const double pitchRatio = pitchThisPath ? pitchPercentToRatio (params.pitchPercent) : 1.0;

    double bpm = params.manualBpm;
    juce::int64 transportStart = fallbackTransportPos;

    if (playHead != nullptr)
    {
        if (auto position = playHead->getPosition())
        {
            bpm = position->getBpm().orFallback (params.manualBpm);
            transportStart = position->getTimeInSamples().orFallback (fallbackTransportPos);
        }
    }

    if (bpm <= 1.0)
        bpm = params.manualBpm;

    const double barFrames = sampleRate * 60.0 / bpm * 4.0; // 4/4 assumed, per the prompt spec

    int pos = 0;
    while (pos < numSamples)
    {
        const juce::int64 absNow = transportStart + pos;
        const auto barIndex = (juce::int64) std::floor ((double) absNow / barFrames);
        const double barPhase = (double) absNow - (double) barIndex * barFrames;
        const double windowLen = barFrames * (double) params.triggerWindow01;
        const double windowStartPhase = barFrames - windowLen;

        if (barIndex != lastBarIndexSeen)
        {
            currentBarPassesChance = random.nextFloat() < params.triggerChance01;
            lastBarIndexSeen = barIndex;
        }

        const bool inWindow = barPhase >= windowStartPhase;
        const bool active = inWindow && currentBarPassesChance;

        if (active && ! dragWindowWasActive)
        {
            // Just entered a fresh window — restart the wander sequence, and
            // source grains from the same window position one bar earlier.
            dragWander.resetForNewWindow();
            dragSamplesIntoGrain = 0;
            dragGrainFrames = 0; // forces recompute on the first grain below

            const auto windowLenSamples = (int) std::round (windowLen);
            const auto prevBarStart = (juce::int64) ((double) (barIndex - 1) * barFrames);
            const auto poolAbsStart = prevBarStart + (juce::int64) std::round (windowStartPhase);

            dragPoolAbsStart = poolAbsStart;
            dragPoolLenSamples = windowLenSamples;
        }

        dragWindowWasActive = active;

        const double samplesToNextEdge = inWindow ? (barFrames - barPhase) : (windowStartPhase - barPhase);
        int chunk = (int) juce::jmax (1.0, std::ceil (samplesToNextEdge));
        chunk = juce::jmin (chunk, numSamples - pos);

        if (active)
        {
            int subPos = 0;
            while (subPos < chunk)
            {
                if (dragSamplesIntoGrain >= dragGrainFrames || dragGrainFrames <= 0)
                {
                    double grainMs, speedLo, speedHi;
                    computeIntensityParams (params.intensity01, grainMs, speedLo, speedHi);
                    dragGrainFrames = msToSamples (grainMs);

                    int runLenMin, runLenMax;
                    computeChopRange (params.chopRate01, runLenMin, runLenMax);

                    dragCurrentSourceAbsStart = pickNextSourceGrain (dragWander, dragPoolAbsStart, dragPoolLenSamples,
                                                                      dragGrainFrames, runLenMin, runLenMax,
                                                                      speedLo, speedHi);
                    dragGrainReadPos = (double) dragCurrentSourceAbsStart;
                    dragSamplesIntoGrain = 0;
                }

                const int grainChunk = juce::jmin (chunk - subPos, dragGrainFrames - dragSamplesIntoGrain);

                if (dragCurrentSourceAbsStart >= 0)
                {
                    if (pitchThisPath)
                    {
                        copyFromHistoryPitched (buffer, pos + subPos, grainChunk, dragGrainReadPos, pitchRatio);
                    }
                    else
                    {
                        const auto srcStart = dragCurrentSourceAbsStart + dragSamplesIntoGrain;
                        if (isAbsRangeAvailable (srcStart, grainChunk))
                            copyFromHistory (buffer, pos + subPos, srcStart, grainChunk);
                    }
                }

                subPos += grainChunk;
                dragSamplesIntoGrain += grainChunk;
            }
        }
        // else: leave the buffer untouched — it still holds the dry input.

        pos += chunk;
    }

    fallbackTransportPos += numSamples;
}

void GrainWanderEngine::applyWholeSignalPitch (juce::AudioBuffer<float>& buffer, float pitchPercent)
{
    const int numSamples = buffer.getNumSamples();
    const auto blockStartAbs = pitchSamplesWritten;

    writeBlockToRing (pitchHistory, pitchHistoryCapacitySamples, pitchSamplesWritten, buffer, numChannels);

    if (pitchPercent == 0.0f)
    {
        wholeSignalEngaged = false;
        return; // bit-exact bypass — buffer already holds the un-pitched mix.
    }

    if (! wholeSignalEngaged)
    {
        wholeSignalReadPos = (double) blockStartAbs;
        wholeSignalEngaged = true;
    }

    const double ratio = pitchPercentToRatio (pitchPercent);
    const auto minAbs = juce::jmax ((juce::int64) 0, pitchSamplesWritten - pitchHistoryCapacitySamples);
    const auto maxAbs = pitchSamplesWritten - 1;

    for (int i = 0; i < numSamples; ++i)
    {
        const auto flooredAbs = (juce::int64) std::floor (wholeSignalReadPos);
        double clampedPos = wholeSignalReadPos;
        if (flooredAbs < minAbs)
            clampedPos = (double) minAbs;
        else if (flooredAbs + 1 > maxAbs)
            clampedPos = (double) juce::jmax (minAbs, maxAbs - 1);

        for (int ch = 0; ch < numChannels && ch < buffer.getNumChannels(); ++ch)
            buffer.setSample (ch, i, readRingInterpolated (pitchHistory, pitchHistoryCapacitySamples, ch, clampedPos));

        wholeSignalReadPos += ratio;
    }
}

void GrainWanderEngine::applySingularity (juce::AudioBuffer<float>& buffer, const Parameters& params)
{
    const int numSamples = buffer.getNumSamples();

    if (params.singularityEngaged && ! singularityWasEngaged)
    {
        // Rising edge: freeze onto whatever's playing right now.
        singularityAnchorAbsStart = juce::jmax ((juce::int64) 0, samplesWritten - singularityWindowLenSamples);
        singularityLoopOffset = 0.0;
        singularityHoldSeconds = 0.0;
    }
    singularityWasEngaged = params.singularityEngaged;

    singularityBlend.setTargetValue (params.singularityEngaged ? 1.0f : 0.0f);

    if (params.singularityEngaged)
        singularityHoldSeconds += (double) numSamples / sampleRate;

    // Exponential decay from normal speed toward a near-static drone — the
    // longer the button is held, the closer to "frozen at the singularity".
    constexpr double speedFloor = 0.02;
    constexpr double timeConstantSeconds = 1.4;
    const double speed = speedFloor + (1.0 - speedFloor) * std::exp (-singularityHoldSeconds / timeConstantSeconds);

    const int windowLen = juce::jmax (1, singularityWindowLenSamples);

    for (int i = 0; i < numSamples; ++i)
    {
        const float blend = singularityBlend.getNextValue();

        if (blend <= 0.0f)
        {
            singularityLoopOffset = std::fmod (singularityLoopOffset, (double) windowLen);
            continue;
        }

        const double readPos = (double) singularityAnchorAbsStart + std::fmod (singularityLoopOffset, (double) windowLen);

        for (int ch = 0; ch < numChannels && ch < buffer.getNumChannels(); ++ch)
        {
            const float frozen = readRingInterpolated (history, historyCapacitySamples, ch, readPos);
            auto* out = buffer.getWritePointer (ch);
            out[i] = out[i] + (frozen - out[i]) * blend;
        }

        singularityLoopOffset += speed;
    }
}

void GrainWanderEngine::process (juce::AudioBuffer<float>& buffer, const Parameters& params, juce::AudioPlayHead* playHead)
{
    const int numSamples = buffer.getNumSamples();

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        dryScratch.copyFrom (ch, 0, buffer, ch, 0, numSamples);

    writeToHistory (buffer);

    if (params.mode == Mode::stretch)
        processStretch (buffer, params);
    else
        processDrag (buffer, params, playHead);

    // Final dry/wet blend, smoothed to avoid zipper noise when Mix is automated.
    mixSmoothed.setTargetValue (params.mix01);
    for (int i = 0; i < numSamples; ++i)
    {
        const float m = mixSmoothed.getNextValue();
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        {
            auto* out = buffer.getWritePointer (ch);
            const float dry = dryScratch.getSample (ch, i);
            out[i] = dry + (out[i] - dry) * m;
        }
    }

    if (params.pitchMode == PitchMode::wholeSignal)
        applyWholeSignalPitch (buffer, params.pitchPercent);

    applySingularity (buffer, params);
}
