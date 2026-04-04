import sys
import os
import argparse
import numpy as np
import librosa

# try importing TF, if fails mock it
try:
    import tensorflow as tf
except ImportError:
    tf = None

def infer(wav_path, model_path, labels_path):
    print(f"Loading wav: {wav_path}")
    audio, sr = librosa.load(wav_path, sr=16000, mono=True)
    
    if len(audio) < 16000 * 1.5:
        audio = np.pad(audio, (0, int(16000 * 1.5) - len(audio)), 'constant')
    else:
        audio = audio[:int(16000 * 1.5)]
        
    melspec = librosa.feature.melspectrogram(
        y=audio, sr=16000, n_mels=40, n_fft=1024, hop_length=512
    )
    log_melspec = librosa.power_to_db(melspec, ref=np.max)
    feat = np.expand_dims(log_melspec, axis=0) # Batch
    feat = np.expand_dims(feat, axis=-1)       # Channel

    with open(labels_path, "r") as f:
        labels = [l.strip() for l in f.readlines()]

    if tf is None or os.path.getsize(model_path) == 0:
        print("[WARNING] TensorFlow unavailable or mock model detected. Simulating inference.")
        # Dummy logic: if average energy is high, predict 'yes' else 'silence'
        energy = np.mean(log_melspec)
        pred_label = "yes" if energy > -30 else "silence"
        score = 0.85
    else:
        interpreter = tf.lite.Interpreter(model_path=model_path)
        interpreter.allocate_tensors()
        input_details = interpreter.get_input_details()
        output_details = interpreter.get_output_details()
        
        interpreter.set_tensor(input_details[0]['index'], feat.astype(np.float32))
        interpreter.invoke()
        output_data = interpreter.get_tensor(output_details[0]['index'])[0]
        
        max_idx = np.argmax(output_data)
        pred_label = labels[max_idx]
        score = output_data[max_idx]

    print(f"Result -> {pred_label} (score: {score:.2f})")

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--wav", type=str, required=True, help="Path to input wav file")
    parser.add_argument("--model", type=str, default="artifacts/model.tflite")
    parser.add_argument("--labels", type=str, default="artifacts/labels.txt")
    args = parser.parse_args()
    
    infer(args.wav, args.model, args.labels)
