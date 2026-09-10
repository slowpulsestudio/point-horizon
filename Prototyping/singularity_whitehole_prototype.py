"""Throwaway offline prototype for a "White hole" Singularity variant.

Disposable: not part of the shipped plugin. Validates a candidate algorithm
on real audio before porting anything into GrainWanderEngine.

Context (existing, shipped "Black hole" behavior, unchanged by this file):
Singularity freezes the last ~90ms of live input and, the longer it's held,
progressively SLOWS that loop toward a near-static drone (Time dilation)
while pitch ramps down toward -50% (Redshift), grain selection converges
toward a single loop (Gravity well), grain length stretches (Spaghettification),
and the wet output feeds back into history (Supernova).

Requested "White hole" variant (the DSP inverse/companion, selected by a new
Black hole / White hole toggle, only affecting what Singularity does while
engaged):
  - Freeze the body indefinitely in a more "digital" way: instead of an
    analog-style exponential glide down to near-static (Time dilation), lock
    onto the frozen window immediately and loop it at a fixed, clocked rate
    (a hard digital freeze/stutter, not a slowdown).
  - End up as a sustained drone.
  - Push toward distortion but "round it off" so it never just blows out -
    a bounded/soft-knee saturator (tanh-based) rather than hard clipping.
  - Character target: "angels of heaven orchestra in unison", not painful
    harsh distortion - approximated here with a small unison/detune stack
    (3 slightly-detuned copies of the frozen loop, summed) so the drone has
    natural chorus-like beating/thickness instead of a single flat tone,
    THEN saturated.

This script sweeps the saturation drive amount (Round 1: full plausible
range including extremes) to find where it stops sounding "rounded/choir-like"
and starts sounding like harsh distortion, before any value is locked in.
"""
import numpy as np
import soundfile as sf
import matplotlib.pyplot as plt
from pathlib import Path

INPUT_FILE = Path(__file__).parent.parent / "Input" / "N2SG_137_Drum_Lost_Full.wav"
OUTPUT_DIR = Path(__file__).parent.parent / "Output" / "singularity-whitehole"

FREEZE_WINDOW_MS = 90.0  # matches the shipped Singularity's singularityWindowLenSamples
HOLD_SECONDS = 6.0
FREEZE_AT_SECONDS = 2.0  # where in the input the freeze grabs its window from

# A few cents of detune per unison layer (ratios close to 1.0), matching the
# "orchestra in unison" brief - subtle pitch drift between layers, not a
# full chord/interval stack.
UNISON_RATIOS = [1.0, 1.0009, 0.9991]


def rms(x: np.ndarray) -> float:
    return float(np.sqrt(np.mean(x.astype(np.float64) ** 2)))


def peak(x: np.ndarray) -> float:
    return float(np.max(np.abs(x)))


def resample_linear(window: np.ndarray, ratio: float, out_len: int) -> np.ndarray:
    """Reads `window` at a constant `ratio` speed, wrapping, for `out_len` output frames."""
    n = len(window)
    return resample_at_positions(window, (np.arange(out_len) * ratio) % n)


def soft_saturate(x: np.ndarray, drive: float) -> np.ndarray:
    """Bounded/soft-knee saturator - tanh normalized so peak stays ~unchanged
    at low drive and the curve rounds off toward +-1 at high drive, instead
    of hard-clipping."""
    if drive <= 0.0:
        return x
    return np.tanh(drive * x) / np.tanh(drive)


def resample_at_positions(window: np.ndarray, read_pos: np.ndarray) -> np.ndarray:
    """Reads `window` (wrapping) at arbitrary, possibly time-varying fractional
    positions - generalises resample_linear() to a non-constant playback rate."""
    n = len(window)
    i0 = np.floor(read_pos).astype(np.int64) % n
    i1 = (i0 + 1) % n
    frac = read_pos - np.floor(read_pos)
    if window.ndim == 1:
        return window[i0] * (1 - frac) + window[i1] * frac
    frac = frac[:, None]
    return window[i0] * (1 - frac) + window[i1] * frac


def apply_black_hole(x: np.ndarray, sr: int) -> tuple[np.ndarray, np.ndarray]:
    """Reference render of the shipped Singularity ("Black hole") behavior,
    for A/B comparison against White hole candidates.

    Approximates its dominant, perceptually-defining mechanic under a
    sustained hold: Time Dilation - the frozen ~90ms window is looped at a
    speed that decays exponentially toward near-static (GrainWanderEngine.cpp's
    applySingularity(): speedFloor=0.02, timeConstantSeconds=1.4). Gravity
    Well / Spaghettification / Redshift / Supernova mainly colour the
    underlying live Stretch/Drag wander, which is almost fully overwritten
    by the frozen loop once singularityBlend nears 1 during a sustained
    hold - so they're not reproduced here; this is a close reference for
    the drone character, not a bit-exact port.
    """
    freeze_frames = max(1, int(round(FREEZE_WINDOW_MS / 1000.0 * sr)))
    freeze_start = int(round(FREEZE_AT_SECONDS * sr))
    window = x[freeze_start:freeze_start + freeze_frames]

    hold_frames = int(round(HOLD_SECONDS * sr))
    dry = x[freeze_start:freeze_start + hold_frames]
    if len(dry) < hold_frames:
        dry = np.pad(dry, [(0, hold_frames - len(dry))] + [(0, 0)] * (x.ndim - 1))

    speed_floor = 0.02
    time_constant_seconds = 1.4
    t = np.arange(hold_frames) / sr
    speed = speed_floor + (1.0 - speed_floor) * np.exp(-t / time_constant_seconds)
    read_pos = np.cumsum(speed) % freeze_frames

    wet = resample_at_positions(window, read_pos)
    return dry, wet


def apply_white_hole(x: np.ndarray, sr: int, drive: float) -> tuple[np.ndarray, np.ndarray]:
    """Returns (dry, wet) of length HOLD_SECONDS starting at FREEZE_AT_SECONDS."""
    freeze_frames = max(1, int(round(FREEZE_WINDOW_MS / 1000.0 * sr)))
    freeze_start = int(round(FREEZE_AT_SECONDS * sr))
    window = x[freeze_start:freeze_start + freeze_frames]

    hold_frames = int(round(HOLD_SECONDS * sr))
    dry = x[freeze_start:freeze_start + hold_frames]
    if len(dry) < hold_frames:
        dry = np.pad(dry, [(0, hold_frames - len(dry))] + [(0, 0)] * (x.ndim - 1))

    # Digital freeze: immediately locked into a fixed-rate loop per unison
    # layer (no exponential slowdown), summed and averaged across layers.
    layers = [resample_linear(window, ratio, hold_frames) for ratio in UNISON_RATIOS]
    drone = np.mean(layers, axis=0)

    wet = soft_saturate(drone, drive)
    return dry, wet


def save_render(name: str, subdir: Path, dry: np.ndarray, wet: np.ndarray, sr: int, save_dry: bool = False):
    subdir.mkdir(parents=True, exist_ok=True)
    sf.write(subdir / f"{name}.wav", wet, sr)
    if save_dry:
        sf.write(subdir / f"{name}_dry.wav", dry, sr)

    mono_dry = dry if dry.ndim == 1 else dry.mean(axis=1)
    mono_wet = wet if wet.ndim == 1 else wet.mean(axis=1)

    zoom_frames = min(len(mono_wet), int(2.0 * sr))
    fig, axes = plt.subplots(2, 1, figsize=(10, 5), sharex=True, sharey=True)
    t = np.arange(zoom_frames) / sr
    axes[0].plot(t, mono_dry[:zoom_frames], linewidth=0.5)
    axes[0].set_title(f"{name} — dry (zoom, first 2s)")
    axes[1].plot(t, mono_wet[:zoom_frames], linewidth=0.5, color="tab:orange")
    axes[1].set_title(f"{name} — wet (zoom, first 2s)")
    axes[1].set_xlabel("seconds")
    fig.tight_layout()
    fig.savefig(subdir / f"{name}_waveform.png", dpi=120)
    plt.close(fig)

    fig, axes = plt.subplots(2, 1, figsize=(10, 6), sharex=True, sharey=True)
    axes[0].specgram(mono_dry, Fs=sr, NFFT=1024, noverlap=512)
    axes[0].set_title(f"{name} — dry spectrogram")
    axes[1].specgram(mono_wet, Fs=sr, NFFT=1024, noverlap=512)
    axes[1].set_title(f"{name} — wet spectrogram")
    axes[1].set_xlabel("seconds")
    fig.tight_layout()
    fig.savefig(subdir / f"{name}_spectrogram.png", dpi=120)
    plt.close(fig)

    print(
        f"{name}: dry RMS={rms(mono_dry):.4f} peak={peak(mono_dry):.4f} | "
        f"wet RMS={rms(mono_wet):.4f} peak={peak(mono_wet):.4f}"
    )


def render_drive_sweep(x: np.ndarray, sr: int):
    subdir = OUTPUT_DIR / "drive_sweep"
    # Round 1: full plausible range, including extremes (0 = no saturation
    # at all, up through hard/obviously-overdriven).
    for drive in [0.0, 1.0, 2.0, 4.0, 8.0, 16.0, 32.0]:
        dry, wet = apply_white_hole(x, sr, drive)
        save_render(f"drive_{drive:05.1f}", subdir, dry, wet, sr)


def render_comparison(x: np.ndarray, sr: int):
    """Dry / Black hole / White hole (drive=2, the candidate asked about) side
    by side, same freeze window and hold length, for direct A/B listening."""
    subdir = OUTPUT_DIR / "comparison"

    dry, black_hole_wet = apply_black_hole(x, sr)
    save_render ("black_hole", subdir, dry, black_hole_wet, sr, save_dry=True)

    dry, white_hole_wet = apply_white_hole(x, sr, drive=2.0)
    save_render ("white_hole_drive_002", subdir, dry, white_hole_wet, sr)


def main():
    x, sr = sf.read(INPUT_FILE, always_2d=False)
    print(f"Loaded {INPUT_FILE.name}: {len(x)} frames @ {sr}Hz, shape={x.shape}")

    print("\n== White hole saturation drive sweep (Round 1, unison detune fixed) ==")
    render_drive_sweep(x, sr)

    print("\n== Comparison: dry vs Black hole vs White hole (drive=2) ==")
    render_comparison(x, sr)

    print(f"\nRenders written to {OUTPUT_DIR}")


if __name__ == "__main__":
    main()
