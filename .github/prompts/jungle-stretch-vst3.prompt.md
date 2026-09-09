---
mode: agent
description: 'Bootstrap and build the Jungle Stretch VST3 audio plugin project'
---
# Project: Jungle Stretch VST3

## What this is

A VST3 audio plugin that ports the "Jungle Stretch" effect from the databend
image-glitching app into a real audio DSP effect. The original effect treats
raw pixel bytes as a flat buffer and applies a 90s-sampler-style granular
time-stretch (think Akai S950/S1000 amen-break chopping): split the buffer
into fixed-size grains, replay them at a wandering local playback speed —
slow patches repeat a grain, fast patches skip ahead — producing the classic
chattery jungle/breakbeat stretch artifact, while the total buffer length
never changes (like chopping a break within a fixed bar: some hits repeat,
others get dropped to compensate).

This project re-implements that same grain-index-wandering algorithm on
real audio samples instead of image bytes, as a real-time VST3 effect.

## Reference algorithm (from the original byte-domain version)

```python
def fx_jungle_stretch(flat, param, rng, ctx):
    intensity = _intensity(param)  # param in [0,1], intensity = param * 2.0
    frame_bytes = max(1, ctx.bpp)
    grain_frames = max(64, int(round(1600 - intensity * 700)))
    grain_bytes = grain_frames * frame_bytes
    num_grains = ctx.usable // grain_bytes
    if num_grains < 2:
        return flat.copy()

    speed_lo = max(0.25, 1.0 - intensity * 0.6)
    speed_hi = 1.0 + intensity * 1.5
    run_len = int(rng.integers(3, 10))
    num_runs = -(-num_grains // run_len)  # ceil division
    run_speeds = rng.uniform(speed_lo, speed_hi, size=num_runs)
    speeds = np.repeat(run_speeds, run_len)[:num_grains]

    src = np.concatenate(([0.0], np.cumsum(speeds)[:-1]))
    src_idx = src.astype(np.int64) % num_grains

    grains = flat[:num_grains * grain_bytes].reshape(num_grains, grain_bytes)
    out = flat.copy()
    out[:num_grains * grain_bytes] = grains[src_idx].reshape(-1)
    return out
```

Key properties to preserve in the audio port:
- The buffer is divided into fixed-size **grains** (not overlapping,
  not crossfaded in the original — a hard-cut chop).
- A **cumulative wandering playback position** (`src`) walks through grain
  *indices* (not sample offsets) at a per-run speed multiplier, wrapping
  around (`% num_grains`) so it always samples from *within* the available
  history — never reads past what exists.
- Speed multipliers are held constant for a **run** of several consecutive
  grains (`run_len` grains, 3–9), then re-rolled — this is what produces the
  "chattery, glitchy jumps" character rather than smooth pitch drift.
- **Total output length == total input length**, always. This is a
  reordering/repeat/skip of existing grains, not a true resample.

## Porting to real-time audio

This cannot work exactly as written in a streaming plugin (the original
operates on one complete, already-captured buffer). Adapt it as follows:

1. **Rolling history buffer.** Maintain a circular buffer of incoming audio
   representing a fixed time window (e.g. a "Loop Length" parameter in
   bars/beats, synced to host tempo if available, or a raw ms value as a
   fallback with no host sync). This window is the audio equivalent of
   `ctx.usable` — the pool of grains available to reorder. Continuously
   overwrite the oldest region as new audio arrives (like a live-looper
   capturing a break).
2. **Grain size** maps `grain_frames` → samples per grain, derived from the
   `Intensity` parameter exactly as above (`1600 - intensity*700`, but scale
   the constants to a sensible sample-rate-relative grain duration instead
   of hardcoded byte-frame counts — expose as a musical/ms value under the
   hood).
3. **Grain-index wandering position** is recomputed per block: keep a
   running cumulative grain-index float, advance it by the current run's
   speed each grain, wrap modulo `num_grains` (the number of grains that fit
   in the current history window).
4. **Hard-cut grain boundaries, no crossfade** — preserve the original's
   raw chop character; do not add grain-window crossfading unless a later
   "Smooth" parameter explicitly asks for it (adding it changes the
   character significantly, so keep it optional/off by default).
5. Output length is not an issue in real-time (it's a continuous stream) —
   the "total length preserved" property becomes "the wandering read
   position never lags or leads the write position by more than the
   history window," i.e. it's just an internal read/write pointer pair into
   a ring buffer, similar to a delay line.

## Parameters (initial set)

- **Intensity** (0–100%): drives grain size (smaller grains = more chopped)
  and widens the speed range, exactly mirroring `_intensity`/`speed_lo`/
  `speed_hi` above.
- **Loop Length**: size of the rolling history window grains are drawn from
  (ms, or synced to host tempo in bars if available).
- **Run Length** (or "Chop Rate"): number of grains per speed-run before
  re-rolling (3–9 in the original — consider exposing as a range or a
  single "randomness" knob that picks within a range).
- **Mix**: dry/wet.
- Optional stretch goals (do not build until the core chop is validated and
  approved): tempo-sync grain size to note divisions; stereo-link vs.
  independent L/R grain wandering for width.

## Creative modes (5 presets on the same core algorithm)

All five modes below reuse the identical grain-index-wandering core
(fixed-size grains, cumulative wandering index, per-run speed multiplier,
modulo wrap into the history window). Nothing about the core algorithm
changes between modes — each mode is just a different combination of
grain-size range, speed-range, run-length distribution, and (for modes 4–5)
one small, explicitly-scoped extension to the wander rule. Expose as a
**Mode** selector; each mode may expose its own sub-range for Intensity
rather than sharing one universal mapping.

1. **Classic Jungle Stretch** (the default, as described above) — moderate
   grain size (~20–70ms), speed range roughly 0.25×–2.5×, run length 3–9
   grains. The baseline amen-break chop character.
2. **Granular Chop** — much smaller grains (~5–20ms) and a wider, more
   extreme speed range (e.g. 0.1×–4×) with short runs (1–4 grains). Produces
   a denser, more chaotic micro-chop/granular-synthesis texture rather than
   discrete audible "hits" repeating.
3. **Skip** — speed multiplier is heavily biased toward 0 (near-frozen,
   e.g. 0–0.15×) for most runs, with occasional short bursts up to 2–3× to
   "catch up." Mimics a scratched CD / stuck-needle skip-and-lurch rather
   than smooth wandering.
4. **Reverse Wander** — extends the wander rule to allow negative speed
   multipliers on some runs (grain index decrements instead of increments),
   still wrapping modulo `num_grains`. Produces intermittent reversed grain
   playback woven into forward playback. Keep the run-based speed model
   identical; only the allowed sign of the speed range changes.
5. **Freeze Glitch** — extends the run-length rule so that, occasionally
   (a low-probability chance per run), a run's speed is forced to exactly 0
   and its run length is drawn from a much longer range (e.g. 20–60 grains
   instead of 3–9), producing an extended freeze/hold on a single grain
   before wandering resumes. All other runs behave as in Classic mode.

Do not build all five before Classic is validated end-to-end per
`workflow/dsp-prototyping` (prototype, tune, and get sign-off on Classic
first, then use the same offline-prototype-then-port process for each
additional mode).

## Workflow

- Use the up-skill bootstrap flow (`/skill-me-up`) first if `.skills` /
  `master-skills.md` don't exist yet. Platform: `platform/juce-vst3-plugin`.
  Recommended workflow skills: `workflow/general`, `workflow/architecture`,
  `workflow/git`, `workflow/dsp-prototyping` (this project is exactly the
  case that skill exists for), `workflow/testing`.
- Per `workflow/dsp-prototyping`: prototype and tune the grain-wandering
  algorithm offline first (Python, on a real breakbeat/loop test file),
  confirm it sounds like the intended jungle/chop artifact and not just
  noise, *before* porting the confirmed algorithm into the JUCE/C++
  real-time plugin.
- Test with real breakbeat/percussion loops, not just sine tones — the
  chop/repeat artifact is only meaningful on transient-rich material.

## Explicitly out of scope for v1

- Pitch-preserving time-stretch (this effect intentionally does NOT
  preserve pitch/formants — the point is the raw chop/repeat artifact, not
  a clean stretch).
- FFT/phase-vocoder approaches — this is a time-domain grain-replay effect
  by design; do not reimplement it as spectral manipulation.
