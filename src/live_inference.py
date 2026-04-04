import time
import os
import numpy as np
import librosa
from .audio_stream import AudioStream

try:
    import tensorflow as tf
except ImportError:
    tf = None

class LiveInference:
    def __init__(self, model_path, labels_path, sample_rate=16000, duration_sec=1.5):
        self.sample_rate = sample_rate
        self.duration_sec = duration_sec
        self.target_samples = int(sample_rate * duration_sec)
        
        self.interpreter = None
        self.mock_mode = False
        
        if tf is None or os.path.getsize(model_path) == 0:
            print("[WARNING] TensorFlow unavailable or empty model detected. Running in MOCK mode.")
            self.mock_mode = True
        else:
            # Load TFLite model
            self.interpreter = tf.lite.Interpreter(model_path=model_path)
            self.interpreter.allocate_tensors()
            self.input_details = self.interpreter.get_input_details()
            self.output_details = self.interpreter.get_output_details()
        
        # Load Labels
        with open(labels_path, "r") as f:
            self.labels = [line.strip() for line in f.readlines() if line.strip()]
            
        self.audio_stream = AudioStream(sample_rate=sample_rate, buffer_duration_sec=2.0)
        self.last_detect_time = 0
        self.is_running = False

    def start_stream(self, device_id=None):
        self.audio_stream.start(device_id)
        self.is_running = True

    def stop_stream(self):
        self.audio_stream.stop()
        self.is_running = False
        
    def _compute_features(self, audio_data):
        # same as train.py
        melspec = librosa.feature.melspectrogram(
            y=audio_data, sr=self.sample_rate, n_mels=40, n_fft=1024, hop_length=512
        )
        log_melspec = librosa.power_to_db(melspec, ref=np.max)
        
        # Add channel dimension to match input shape
        feat = np.expand_dims(log_melspec, axis=0) # Batch
        feat = np.expand_dims(feat, axis=-1)       # Channel
        return feat.astype(np.float32)

    def process_frame(self, threshold=0.7, cooldown_sec=2.0):
        if not self.is_running:
            return None, 0.0
            
        # Get latest 1.5 seconds from ring buffer
        audio_data = self.audio_stream.get_last_n_samples(self.target_samples)
        
        features = self._compute_features(audio_data)
        
        if self.mock_mode:
            # Simulate inference: high energy means a voice (simulate "yes" randomly)
            energy = np.mean(features)
            if energy > -30.0:
                label = "yes"
                score = np.random.uniform(0.75, 0.95)
            else:
                label = "silence"
                score = np.random.uniform(0.8, 1.0)
        else:
            # Real Inference
            self.interpreter.set_tensor(self.input_details[0]['index'], features)
            self.interpreter.invoke()
            output_data = self.interpreter.get_tensor(self.output_details[0]['index'])[0]
            
            max_idx = np.argmax(output_data)
            label = self.labels[max_idx]
            score = output_data[max_idx]
        
        # Cooldown & Thresholding logic for "yes"
        now = time.time()
        is_trigger = False
        if label == "yes" and score >= threshold:
            if (now - self.last_detect_time) > cooldown_sec:
                self.last_detect_time = now
                is_trigger = True
                
        return label, score, is_trigger

