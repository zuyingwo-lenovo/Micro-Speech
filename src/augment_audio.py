import argparse
import os
import sys
import json
import csv
import soundfile as sf
import numpy as np
import librosa
import warnings
warnings.filterwarnings("ignore", module="librosa")
from audiomentations import (
    Compose, AddGaussianNoise, TimeStretch, PitchShift, Shift, Gain, AddBackgroundNoise
)
import random

def check_audio_validity(audio):
    if np.any(np.isnan(audio)):
        return False, "NaN values detected"
    if np.any(np.isinf(audio)):
        return False, "Inf values detected"
    if np.max(np.abs(audio)) >= 1.0:
        return False, "Clipping detected"
    return True, "OK"

def get_augmenter(seed, noise_dir=None, label="yes"):
    # Conservative parameters for Wake Word detection ("yes")
    transforms = [
        AddGaussianNoise(min_amplitude=0.001, max_amplitude=0.015, p=0.5),
        TimeStretch(min_rate=0.9, max_rate=1.1, p=0.5),
        PitchShift(min_semitones=-2, max_semitones=2, p=0.5),
        Shift(min_shift=-0.1, max_shift=0.1, p=0.5),
        Gain(min_gain_db=-6, max_gain_db=6, p=0.5)
    ]
    
    if noise_dir and os.path.exists(noise_dir) and len(os.listdir(noise_dir)) > 0:
        transforms.append(
            AddBackgroundNoise(sounds_path=noise_dir, min_snr_in_db=5, max_snr_in_db=20, p=0.5)
        )
        
    return Compose(transforms=transforms)

def setup_args():
    parser = argparse.ArgumentParser(description="Audio Data Augmentation using audiomentations")
    parser.add_argument("--input", type=str, required=True, help="Input file or directory (if dataset-mode)")
    parser.add_argument("--output-dir", type=str, required=True, help="Output directory")
    parser.add_argument("--num-aug", type=int, default=5, help="Number of augmentations per input file")
    parser.add_argument("--label", type=str, default="yes", help="Label of the class (used for file naming and slight policy tuning)")
    parser.add_argument("--dataset-mode", action="store_true", help="If set, input is assumed to be a dataset root directory containing label subdirs")
    parser.add_argument("--noise-dir", type=str, default=None, help="Directory containing background noises (.wav)")
    parser.add_argument("--sample-rate", type=int, default=16000, help="Expected sample rate")
    parser.add_argument("--seed", type=int, default=42, help="Random seed for reproducibility")
    return parser.parse_args()

def process_file(input_file, output_dir, augmenter, num_aug, label, args, manifest, summary):
    try:
        audio, sr = librosa.load(input_file, sr=args.sample_rate, mono=True)
    except Exception as e:
        print(f"Error reading {input_file}: {e}")
        summary["errors"] += 1
        return
        
    basename = os.path.splitext(os.path.basename(input_file))[0]
    os.makedirs(output_dir, exist_ok=True)
    
    summary["original_files"] += 1
    
    for i in range(num_aug):
        try:
            # audiomentations expects float32
            audio_f32 = audio.astype(np.float32)
            augmented = augmenter(samples=audio_f32, sample_rate=sr)
            
            valid, msg = check_audio_validity(augmented)
            if not valid:
                print(f"Skipping augmented sample {i} for {input_file}: {msg}")
                summary["skipped"] += 1
                continue
                
            out_filename = f"{basename}_aug_{i:04d}.wav"
            out_filepath = os.path.join(output_dir, out_filename)
            
            sf.write(out_filepath, augmented, sr)
            
            duration = len(augmented) / sr
            
            transforms_applied = "AddGaussianNoise,TimeStretch,PitchShift,Shift,Gain"
            if args.noise_dir:
                transforms_applied += ",AddBackgroundNoise"
                
            manifest.append({
                "original_file": input_file,
                "augmented_file": out_filepath,
                "label": label,
                "sample_rate": sr,
                "duration_sec": round(duration, 3),
                "applied_transforms": transforms_applied,
                "random_seed": args.seed
            })
            
            summary["generated_files"] += 1
            summary["by_label"][label] = summary["by_label"].get(label, 0) + 1
            
        except Exception as e:
            print(f"Error augmenting {input_file} (iter {i}): {e}")
            summary["errors"] += 1

def main():
    args = setup_args()
    
    # Reproducibility
    random.seed(args.seed)
    np.random.seed(args.seed)
    
    augmenter = get_augmenter(args.seed, args.noise_dir, args.label)
    
    manifest = []
    summary = {
        "original_files": 0,
        "generated_files": 0,
        "skipped": 0,
        "errors": 0,
        "by_label": {},
        "config": {
            "num_aug": args.num_aug,
            "sample_rate": args.sample_rate,
            "seed": args.seed,
            "dataset_mode": args.dataset_mode
        }
    }
    
    artifacts_dir = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "artifacts")
    os.makedirs(artifacts_dir, exist_ok=True)
    
    if args.dataset_mode:
        if not os.path.isdir(args.input):
            print(f"Error: dataset-mode expects a directory path for --input, got {args.input}")
            sys.exit(1)
            
        for root, dirs, files in os.walk(args.input):
            for file in files:
                if file.lower().endswith((".wav", ".m4a")):
                    parent_dir = os.path.basename(root)
                    label = parent_dir
                    
                    if label == "silence":
                        continue 
                        
                    input_filepath = os.path.join(root, file)
                    out_subdir = os.path.join(args.output_dir, label)
                    aug = get_augmenter(args.seed, args.noise_dir, label)
                    
                    process_file(input_filepath, out_subdir, aug, args.num_aug, label, args, manifest, summary)
    else:
        if not os.path.exists(args.input):
            print(f"Error: input file {args.input} not found.")
            sys.exit(1)
            
        process_file(args.input, args.output_dir, augmenter, args.num_aug, args.label, args, manifest, summary)
        
    manifest_path = os.path.join(artifacts_dir, "augmentation_manifest.csv")
    if len(manifest) > 0:
        keys = manifest[0].keys()
        with open(manifest_path, 'w', newline='', encoding='utf-8') as f:
            writer = csv.DictWriter(f, fieldnames=keys)
            writer.writeheader()
            writer.writerows(manifest)
            
    summary_path = os.path.join(artifacts_dir, "augmentation_summary.json")
    with open(summary_path, 'w', encoding='utf-8') as f:
        json.dump(summary, f, indent=4)
        
    print(f"\\nDone! Augmented {summary['generated_files']} files from {summary['original_files']} original files.")
    print(f"Manifest written to {manifest_path}")
    print(f"Summary written to {summary_path}")

if __name__ == "__main__":
    main()
