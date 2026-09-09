"""Throwaway offline prototype for the Jungle Stretch grain-wandering algorithm.

Validates the "Classic" mode artifact on real audio before porting to JUCE/C++.
Disposable: not part of the shipped plugin.
"""
import numpy as np
import soundfile as sf
import matplotlib.pyplot as plt
from pathlib import Path

INPUT_FILE = Path(__file__).parent.parent / "Input" / "N2SG_137_Drum_Lost_Full.wav"
OUTPUT_DIR = Path(__file__).parent.parent / "Output" / "jungle-stretch"


def intensity_to_params(intensity: float):
    """Classic-mode mapping: intensity in [0,1] -> grain size (ms) and speed range."""
    grain_ms = 70.0 - intensity * 50.0  # 70ms (gentle) -> 20ms (chopped)
    intensity_scaled = intensity * 2.0  # mirrors the original param*2.0 convention
    speed_lo = max(0.25, 1.0 - intensity_scaled * 0.6)
    speed_hi = 1.0 + intensity_scaled * 1.5
    return grain_ms, speed_lo, speed_hi


def jungle_stretch(x: np.ndarray, sr: int, grain_ms: float, speed_lo: float,
                    speed_hi: float, run_len_range: tuple[int, int], rng: np.random.Generator):
    """Ports fx_jungle_stretch's grain-index-wandering core onto real audio samples."""
    grain_frames = max(1, int(round(grain_ms / 1000.0 * sr)))
    n = len(x)
    num_grains = n // grain_frames
    if num_grains < 2:
        return x.copy()

    run_len = int(rng.integers(run_len_range[0], run_len_range[1]))
    num_runs = -(-num_grains // run_len)  # ceil division
    run_speeds = rng.uniform(speed_lo, speed_hi, size=num_runs)
    speeds = np.repeat(run_speeds, run_len)[:num_grains]

    src = np.concatenate(([0.0], np.cumsum(speeds)[:-1]))
    src_idx = src.astype(np.int64) % num_grains

    usable = num_grains * grain_frames
    grains = x[:usable].reshape(num_grains, grain_frames, *x.shape[1:])
    out = x.copy()
    out[:usable] = grains[src_idx].reshape(usable, *x.shape[1:])
    return out


def rms(x: np.ndarray) -> float:
    return float(np.sqrt(np.mean(x.astype(np.float64) ** 2)))


def peak(x: np.ndarray) -> float:
    return float(np.max(np.abs(x)))


def save_render(name: str, subdir: Path, dry: np.ndarray, wet: np.ndarray, sr: int):
    subdir.mkdir(parents=True, exist_ok=True)
    sf.write(subdir / f"{name}.wav", wet, sr)

    mono_dry = dry if dry.ndim == 1 else dry.mean(axis=1)
    mono_wet = wet if wet.ndim == 1 else wet.mean(axis=1)

    # Zoomed waveform around a representative early transient (first 2s).
    zoom_frames = min(len(mono_dry), int(2.0 * sr))
    fig, axes = plt.subplots(2, 1, figsize=(10, 5), sharex=True, sharey=True)
    t = np.arange(zoom_frames) / sr
    axes[0].plot(t, mono_dry[:zoom_frames], linewidth=0.5)
    axes[0].set_title(f"{name} — dry (zoom)")
    axes[1].plot(t, mono_wet[:zoom_frames], linewidth=0.5, color="tab:orange")
    axes[1].set_title(f"{name} — wet (zoom)")
    axes[1].set_xlabel("seconds")
    fig.tight_layout()
    fig.savefig(subdir / f"{name}_waveform.png", dpi=120)
    plt.close(fig)

    # Before/after spectrograms.
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


def render_intensity_sweep(x: np.ndarray, sr: int):
    subdir = OUTPUT_DIR / "intensity_sweep"
    for intensity in [0.0, 0.25, 0.5, 0.75, 1.0]:
        grain_ms, speed_lo, speed_hi = intensity_to_params(intensity)
        rng = np.random.default_rng(42)  # fixed seed isolates intensity's effect
        wet = jungle_stretch(x, sr, grain_ms, speed_lo, speed_hi, (3, 10), rng)
        print(f"  intensity={intensity:.2f} grain_ms={grain_ms:.1f} speed=[{speed_lo:.2f},{speed_hi:.2f}]")
        save_render(f"intensity_{intensity:.2f}", subdir, x, wet, sr)


def render_runlen_sweep(x: np.ndarray, sr: int):
    subdir = OUTPUT_DIR / "run_length_sweep"
    grain_ms, speed_lo, speed_hi = intensity_to_params(0.5)  # neutral baseline
    for run_len in [2, 5, 10, 20, 40]:
        rng = np.random.default_rng(42)
        wet = jungle_stretch(x, sr, grain_ms, speed_lo, speed_hi, (run_len, run_len + 1), rng)
        save_render(f"run_len_{run_len:02d}", subdir, x, wet, sr)


def apply_windowed(x: np.ndarray, sr: int, bpm: float, window_frac: float, grain_ms: float,
                    speed_lo: float, speed_hi: float, run_len_range: tuple[int, int],
                    rng: np.random.Generator, per_beat: bool = False, every_n_bars: int = 1,
                    beats_per_bar: int = 4, chance: float = 1.0):
    """Confines the same jungle_stretch() core to a trailing fraction of each bar/beat.

    `chance` (0-1) independently rolls per eligible bar/beat, so an eligible
    window is skipped (left dry) unless the roll succeeds. Rest of the audio
    outside the window is always left untouched.
    """
    beat_frames = sr * 60.0 / bpm
    unit_frames = int(round(beat_frames if per_beat else beat_frames * beats_per_bar))
    grain_frames = max(1, int(round(grain_ms / 1000.0 * sr)))
    n = len(x)
    out = x.copy()
    pos = 0
    unit_index = 0
    while pos < n:
        unit_end = min(pos + unit_frames, n)
        eligible = per_beat or (unit_index % every_n_bars == every_n_bars - 1)
        if eligible and rng.random() < chance:
            window_start = min(pos + int(round(unit_frames * (1 - window_frac))), unit_end)
            seg = x[window_start:unit_end]
            if len(seg) > grain_frames * 2:
                out[window_start:unit_end] = jungle_stretch(
                    seg, sr, grain_ms, speed_lo, speed_hi, run_len_range, rng
                )
        pos = unit_end
        unit_index += 1
    return out


def render_chance_sweep(x: np.ndarray, sr: int, bpm: float):
    subdir = OUTPUT_DIR / "chance_sweep"
    grain_ms, speed_lo, speed_hi = intensity_to_params(0.4)
    for chance in [0.25, 0.5, 0.75, 1.0]:
        rng = np.random.default_rng(42)
        wet = apply_windowed(x, sr, bpm, 0.25, grain_ms, speed_lo, speed_hi, (3, 10), rng, chance=chance)
        save_render(f"drag_chance_{int(chance * 100):03d}", subdir, x, wet, sr)


def render_position_gated(x: np.ndarray, sr: int, bpm: float):
    subdir = OUTPUT_DIR / "position_gated"
    # Tempered baseline per Round 1 feedback — avoid the short-grain/wide-speed extreme.
    grain_ms, speed_lo, speed_hi = intensity_to_params(0.4)

    rng = np.random.default_rng(42)
    wet = apply_windowed(x, sr, bpm, 0.25, grain_ms, speed_lo, speed_hi, (3, 10), rng)
    save_render("drag_last_qtr_bar", subdir, x, wet, sr)

    rng = np.random.default_rng(42)
    # Per-beat window is much shorter than a bar — needs a smaller grain to fit at least 2 grains.
    stumble_grain_ms = 15.0
    wet = apply_windowed(x, sr, bpm, 0.3, stumble_grain_ms, speed_lo, speed_hi, (3, 10), rng, per_beat=True)
    save_render("stumble_per_beat", subdir, x, wet, sr)

    rng = np.random.default_rng(42)
    wet = apply_windowed(x, sr, bpm, 0.25, grain_ms, speed_lo, speed_hi, (3, 10), rng, every_n_bars=4)
    save_render("turnaround_every_4_bars", subdir, x, wet, sr)

    rng = np.random.default_rng(42)
    # Half-Time Drop: low sustained speed, one run spanning the whole window (huge run_len).
    wet = apply_windowed(x, sr, bpm, 0.25, grain_ms, 0.05, 0.3, (10_000, 10_001), rng)
    save_render("halftime_drop_last_qtr_bar", subdir, x, wet, sr)


def main():
    x, sr = sf.read(INPUT_FILE, always_2d=False)
    print(f"Loaded {INPUT_FILE.name}: {len(x)} frames @ {sr}Hz, shape={x.shape}")

    print("\n== Intensity sweep (Round 1, run length fixed at 3-9) ==")
    render_intensity_sweep(x, sr)

    print("\n== Run length sweep (Round 1, intensity fixed at 0.5) ==")
    render_runlen_sweep(x, sr)

    print("\n== Position-gated modes: Drag, Stumble, Turnaround, Half-Time Drop (BPM=137, 4/4 assumed) ==")
    render_position_gated(x, sr, bpm=137.0)

    print("\n== Trigger Chance sweep (Round 1, Drag mode, 25/50/75/100%) ==")
    render_chance_sweep(x, sr, bpm=137.0)

    print(f"\nRenders written to {OUTPUT_DIR}")


if __name__ == "__main__":
    main()
