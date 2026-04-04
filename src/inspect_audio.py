import argparse
import os
import glob
import soundfile as sf
import json

def setup_args():
    parser = argparse.ArgumentParser(description="Inspect audio files in a directory")
    parser.add_argument("--input", type=str, required=True, help="Directory containing .wav files")
    return parser.parse_args()

def main():
    args = setup_args()
    if not os.path.isdir(args.input):
        print(f"Error: {args.input} is not a directory.")
        return

    stats = {
        "total_files": 0,
        "sample_rates": set(),
        "channels": set(),
        "durations_sec": [],
        "by_label": {}
    }

    wav_files = glob.glob(os.path.join(args.input, "**", "*.wav"), recursive=True)
    stats["total_files"] = len(wav_files)

    for f in wav_files:
        try:
            info = sf.info(f)
            stats["sample_rates"].add(info.samplerate)
            stats["channels"].add(info.channels)
            
            duration = info.frames / info.samplerate
            stats["durations_sec"].append(duration)
            
            label = os.path.basename(os.path.dirname(f))
            stats["by_label"][label] = stats["by_label"].get(label, 0) + 1
        except Exception as e:
            print(f"Error reading {f}: {e}")

    if not wav_files:
        print("No .wav files found.")
        return

    stats["sample_rates"] = list(stats["sample_rates"])
    stats["channels"] = list(stats["channels"])
    
    durations = stats.pop("durations_sec")
    stats["duration_stats"] = {
        "min_sec": round(min(durations), 3),
        "max_sec": round(max(durations), 3),
        "avg_sec": round(sum(durations) / len(durations), 3)
    }

    print("\n--- Audio Inspection Report ---")
    print(json.dumps(stats, indent=4))

if __name__ == "__main__":
    main()
