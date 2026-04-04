import os
import json
import numpy as np
import tensorflow as tf
from tensorflow.keras import layers, models
import librosa
import soundfile as sf
import matplotlib.pyplot as plt
import seaborn as sns
from sklearn.metrics import classification_report, confusion_matrix

SAMPLE_RATE = 16000
DURATION_SEC = 1.5
TARGET_SAMPLES = int(SAMPLE_RATE * DURATION_SEC)

# Feature extraction params
N_MELS = 40
N_FFT = 1024
HOP_LENGTH = 512

DATA_DIR = "data"
ARTIFACTS_DIR = "artifacts"
CATEGORIES = ["yes", "unknown", "silence"]

def compute_mel_spectrogram(audio_data):
    # compute log-Mel Spectrogram
    melspec = librosa.feature.melspectrogram(
        y=audio_data, sr=SAMPLE_RATE, n_mels=N_MELS, n_fft=N_FFT, hop_length=HOP_LENGTH
    )
    log_melspec = librosa.power_to_db(melspec, ref=np.max)
    # Shape: (N_MELS, T)
    return log_melspec

def load_data(split):
    X = []
    y = []
    for label_idx, cat in enumerate(CATEGORIES):
        cat_dir = os.path.join(DATA_DIR, split, cat)
        if not os.path.exists(cat_dir): continue
        for fn in os.listdir(cat_dir):
            if fn.endswith('.wav'):
                filepath = os.path.join(cat_dir, fn)
                audio, _ = librosa.load(filepath, sr=SAMPLE_RATE, mono=True)
                if len(audio) != TARGET_SAMPLES:
                    # Pad/Trim to exactly TARGET_SAMPLES
                    if len(audio) < TARGET_SAMPLES:
                        audio = np.pad(audio, (0, TARGET_SAMPLES - len(audio)), 'constant')
                    else:
                        audio = audio[:TARGET_SAMPLES]
                feat = compute_mel_spectrogram(audio)
                X.append(feat)
                y.append(label_idx)
    
    X = np.array(X)
    y = np.array(y)
    
    # Expand dims for CNN channel: (Batch, N_MELS, Time, 1)
    if len(X) > 0:
        X = np.expand_dims(X, axis=-1)
    return X, y

def build_model(input_shape, num_classes):
    model = models.Sequential([
        layers.Input(shape=input_shape),
        # 1st Conv block
        layers.Conv2D(16, (3, 3), activation='relu', padding='same'),
        layers.MaxPooling2D((2, 2)),
        layers.BatchNormalization(),
        # 2nd Conv block
        layers.Conv2D(32, (3, 3), activation='relu', padding='same'),
        layers.MaxPooling2D((2, 2)),
        layers.BatchNormalization(),
        # Flatten and Dense
        layers.Flatten(),
        layers.Dense(64, activation='relu'),
        layers.Dropout(0.3),
        layers.Dense(num_classes, activation='softmax')
    ])
    
    model.compile(optimizer='adam',
                  loss='sparse_categorical_crossentropy',
                  metrics=['accuracy'])
    return model

def export_tflite(model, model_path):
    # Convert using TFLiteConverter
    converter = tf.lite.TFLiteConverter.from_keras_model(model)
    # Optional: Enable default optimizations (quantization)
    converter.optimizations = [tf.lite.Optimize.DEFAULT]
    tflite_model = converter.convert()
    
    with open(model_path, 'wb') as f:
        f.write(tflite_model)
    print(f"TFLite model exported to {model_path} (Size: {len(tflite_model)} bytes)")

def evaluate_model(model, X_test, y_test):
    predictions = model.predict(X_test)
    y_pred = np.argmax(predictions, axis=1)
    
    # Classification report
    report = classification_report(y_test, y_pred, target_names=CATEGORIES, output_dict=True)
    report_text = classification_report(y_test, y_pred, target_names=CATEGORIES)
    print("Evaluation Report:\n", report_text)
    
    # Confusion Matrix
    cm = confusion_matrix(y_test, y_pred)
    plt.figure(figsize=(8,6))
    sns.heatmap(cm, annot=True, fmt='d', cmap='Blues', xticklabels=CATEGORIES, yticklabels=CATEGORIES)
    plt.ylabel('Actual')
    plt.xlabel('Predicted')
    plt.title('Confusion Matrix')
    cm_path = os.path.join(ARTIFACTS_DIR, "confusion_matrix.png")
    plt.savefig(cm_path)
    plt.close()
    print(f"Confusion matrix saved to {cm_path}")
    
    # Save Report
    with open(os.path.join(ARTIFACTS_DIR, "training_metrics.json"), "w") as f:
        json.dump(report, f, indent=4)
        
    with open(os.path.join(ARTIFACTS_DIR, "eval_report.md"), "w") as f:
        f.write(f"# Evaluation Report\n\n```\n{report_text}\n```\n\n![Confusion Matrix](./confusion_matrix.png)")

if __name__ == "__main__":
    os.makedirs(ARTIFACTS_DIR, exist_ok=True)
    
    print("Loading data...")
    X_train, y_train = load_data("train")
    X_val, y_val = load_data("val")
    X_test, y_test = load_data("test")
    
    print(f"Train samples: {len(X_train)}")
    print(f"Val samples: {len(X_val)}")
    print(f"Test samples: {len(X_test)}")
    
    if len(X_train) == 0:
        print("No training data found. Run prepare_dataset.py first.")
        exit(1)
        
    input_shape = X_train.shape[1:]
    print("Input shape:", input_shape)
    
    print("Building model...")
    model = build_model(input_shape, len(CATEGORIES))
    model.summary()
    
    print("Training model...")
    early_stop = tf.keras.callbacks.EarlyStopping(monitor='val_loss', patience=5, restore_best_weights=True)
    history = model.fit(
        X_train, y_train,
        validation_data=(X_val, y_val),
        epochs=30,
        batch_size=16,
        callbacks=[early_stop]
    )
    
    # Write Labels
    labels_path = os.path.join(ARTIFACTS_DIR, "labels.txt")
    with open(labels_path, "w") as f:
        for c in CATEGORIES:
            f.write(f"{c}\n")
    print(f"Labels saved to {labels_path}")
    
    print("Evaluating model...")
    evaluate_model(model, X_test, y_test)
    
    model_tflite_path = os.path.join(ARTIFACTS_DIR, "model.tflite")
    export_tflite(model, model_tflite_path)
    print("Training pipeline complete.")
