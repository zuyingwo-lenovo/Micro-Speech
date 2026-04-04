import os
import numpy as np
import soundfile as sf
import random

SAMPLE_RATE = 16000
DURATION_SEC = 1.5
TARGET_SAMPLES = int(SAMPLE_RATE * DURATION_SEC)

DATA_DIR = "data"
CATEGORIES = ["yes", "unknown", "silence"]
SPLITS = ["train", "val", "test"]

def init_directories():
    for split in SPLITS:
        for cat in CATEGORIES:
            os.makedirs(os.path.join(DATA_DIR, split, cat), exist_ok=True)

def generate_wave(filepath, freqs, duration, sr=16000, noise_level=0.01):
    t = np.linspace(0, duration, int(sr * duration), False)
    signal = np.zeros_like(t)
    for f in freqs:
        signal += np.sin(f * 2 * np.pi * t)
    
    # normalize and add noise
    if len(freqs) > 0:
        signal = signal / len(freqs)
    
    signal += np.random.normal(0, noise_level, len(signal))
    sf.write(filepath, signal.astype(np.float32), sr)

def generate_dummy_dataset():
    print("Generating simulated signals for pipeline testing...")
    
    # "yes" -> 440 Hz and 880 Hz complex tone
    # "unknown" -> 300 Hz and 600 Hz complex tone
    # "silence" -> pure noise
    
    config = {
        "yes": [440, 880],
        "unknown": [300, 600],
        "silence": []
    }
    
    for split in SPLITS:
        samples_per_class = 100 if split == "train" else 20
        for cat in CATEGORIES:
            for i in range(samples_per_class):
                # add slight frequency variations
                freqs = [f + random.uniform(-10, 10) for f in config[cat]]
                # add amplitude variations
                filepath = os.path.join(DATA_DIR, split, cat, f"{cat}_{i:04d}.wav")
                generate_wave(filepath, freqs, DURATION_SEC, SAMPLE_RATE, noise_level=0.05)

if __name__ == "__main__":
    init_directories()
    generate_dummy_dataset()
    print("Dataset generation complete.")
