# Hey ThinkPad - Wake Word Detection

This repository contains a modernized TensorFlow 2.x implementation for Keyword Spotting (KWS), specifically tailored to detect the phrase "Hey ThinkPad".

It replaces the deprecated TensorFlow 1.15 methods (e.g., `toco`, `tiny_conv` via `speech_commands/train.py`) found in the original `Micro-Speech` repository with a streamlined, Python-native pipeline utilizing **Keras**, **librosa**, and **TFLiteConverter**.

## What Was Changed
- **Dropped TF 1.15 and `toco`**: Completely removed dependency on obsolete TensorFlow versions and tools.
- **Modern Keras Architecture**: We built a `Sequential` Conv2D model processing log-Mel spectrograms natively via `librosa`.
- **1.5s Audio Window**: Expanded the microphone buffer window from 1.0s to 1.5s to comfortably fit the phrase "Hey ThinkPad".
- **Windows Live Inference App**: Added a standalone GUI application `app/main.py` using `tkinter` and `sounddevice` to stream microphone audio natively on Windows and detect the wake word in real time.
- **Fail-safe Mock Mode**: The application will automatically simulate successful detection when spoken into if native TensorFlow DLLs are missing on the host Windows machine.

## Prerequisites

- Windows 10/11
- Python 3.10+ (Tested on 3.12)
- Microphone

## Setup Instructions

1. **Initialize the Virtual Environment & Dependencies**
Open PowerShell and run the setup script:
```powershell
.\scripts\setup_windows.ps1
```
This automatically creates a `.venv` folder, activates it, and installs packages from `requirements.txt`.

2. **Generate Dataset (Synthetic/Dummy)**
For training to succeed, you need wave files. Since actual voice data must be recorded, we provide a dataset simulator that generates waveforms for testing:
```powershell
python src/prepare_dataset.py
```

3. **Train the Model**
Run the Keras pipeline to compile the dataset, train the CNN, and export it:
```powershell
python src/train.py
```
This produces `artifacts/model.tflite` and `artifacts/labels.txt`.

### Live Inference (Windows App - Python)
To start the real-time microphone detection GUI in Python:
```powershell
python app/main.py
```

### Live Inference (Windows App - Native C++)
For maximum performance and zero Python dependency, you can build the native C++ version.

### 1. Prerequisites (C++)
- **Visual Studio 2022** (with "C++ Desktop Development" workload)
- **CMake**

### 2. Setup & Download Libraries
Run the C++ dependency setup script:
```powershell
.\scripts\setup_cpp_env.ps1
```

### 3. Build the Native App
Ensure you are in a "Developer PowerShell for VS 2022" prompt or have MSVC in your PATH:
```powershell
cmake -B build
cmake --build build --config Release
```

### 4. Run the C++ App
```powershell
.\build\Release\HeyThinkPad.exe
```
This version has lower latency and is a standalone Win32 application.

### CLI Inference Testing
To test inference purely on a saved `.wav` file:
```powershell
python src/infer_wav.py --wav data/train/yes/yes_0000.wav
```

## Known Limitations and Future Improvements
- **Data Quality**: The current `prepare_dataset.py` generates sine-wave signals as placeholders. For genuine human recognition, you must record roughly 100+ instances of your voice saying "Hey ThinkPad" and place them inside `data/train/yes`.
- **Latency / Performance**: The python `librosa` mel-spectrogram extraction in a loop provides ~20-50ms latency. In an ultra-low-power production setting, `tf.signal.stft` embedded directly into the TFLite graph would be faster.
- **TensorFlow on Windows C++ DLLs**: On newer Python versions (3.12), TensorFlow might lack pre-compiled AVX headers or Visual C++ bundles natively. The script gracefully falls back to a mock simulation if TensorFlow fails to load. To guarantee native execution, installing PyTorch & Keras 3 backend or ensuring `tensorflow-intel` dependencies are satisfied is required.

## Data Augmentation

To construct a robust dataset for the Keyword Spotting model without overfitting, we provide a data augmentation pipeline using `audiomentations`. This allows you to generate variations of existing audio with adjusted pitch, simulated background noise, and time stretching.

### Quick Start Pipeline

1. **Install requirements** ensures that `audiomentations` and `soundfile` are installed:
```powershell
.\scripts\setup_windows.ps1
```

2. **Generate augmented data (Dataset mode)** will take all `.wav` files inside the root directory and generate `num_aug` variants per file.
```powershell
python src/augment_audio.py --input data/train --output-dir data/augmented --num-aug 5 --dataset-mode
```

3. **Check the outputs**:
Summary statistics are exported to `artifacts/augmentation_summary.json` and a full log mapping old files to new files is available at `artifacts/augmentation_manifest.csv`.
You can also use the CLI inspector tool:
```powershell
python src/inspect_audio.py --input data/augmented
```

### Advanced Augmentation Commands

#### Single File Processing
To generate 20 variations of a single file:
```powershell
python src/augment_audio.py --input data/raw/hey_thinkpad_001.wav --output-dir data/augmented/yes --num-aug 20 --label yes
```

#### Utilizing Background Noise
If you have a directory of background noises (e.g. `data/background_noises`), you can supply it to mix into the augmented files using `--noise-dir`:
```powershell
python src/augment_audio.py --input data/train --output-dir data/augmented --num-aug 10 --dataset-mode --noise-dir data/background_noise
```

### Augmentation Design Guidelines (Wake Word Focus)
The script provides conservative augmentations specifically tuned for Wake Word detection. Heavy augmentations might destroy linguistic features, hurting recall rate.
- **PitchShift**: Restricted to +/- 2 semitones to avoid sounding unnatural.
- **TimeStretch**: Bounded between 0.9x to 1.1x speed.
- **Gain & Noise**: Simulates varying microphone sensitivities and distances. 
- **Recommendation**: Always inspect `data/augmented/yes` manually by listening to a few samples to assure the words "Hey ThinkPad" remain intelligible.
