import sounddevice as sf
import sounddevice as sd
import numpy as np
import threading

class AudioStream:
    def __init__(self, sample_rate=16000, buffer_duration_sec=2.0):
        self.sample_rate = sample_rate
        self.buffer_size = int(sample_rate * buffer_duration_sec)
        self.ring_buffer = np.zeros(self.buffer_size, dtype=np.float32)
        self.write_index = 0
        self.lock = threading.Lock()
        self.stream = None

    def _audio_callback(self, indata, frames, time, status):
        if status:
            pass # ignore warnings/errors for simplicity in this demo
        
        # Flatten to 1D
        indata_mono = indata[:, 0]
        
        with self.lock:
            # If the new data is larger than buffer, keep only the latest
            if frames >= self.buffer_size:
                self.ring_buffer[:] = indata_mono[-self.buffer_size:]
                self.write_index = 0
            else:
                end_idx = self.write_index + frames
                if end_idx <= self.buffer_size:
                    self.ring_buffer[self.write_index:end_idx] = indata_mono
                    self.write_index = end_idx % self.buffer_size
                else:
                    # Wraparound
                    part1_len = self.buffer_size - self.write_index
                    part2_len = frames - part1_len
                    self.ring_buffer[self.write_index:] = indata_mono[:part1_len]
                    self.ring_buffer[:part2_len] = indata_mono[part1_len:]
                    self.write_index = part2_len

    def start(self, device_id=None):
        self.stream = sd.InputStream(
            samplerate=self.sample_rate,
            channels=1,
            dtype='float32',
            device=device_id,
            callback=self._audio_callback
        )
        self.stream.start()

    def stop(self):
        if self.stream:
            self.stream.stop()
            self.stream.close()
            self.stream = None

    def get_last_n_samples(self, n):
        with self.lock:
            if n > self.buffer_size:
                raise ValueError("Requested samples exceed buffer size.")
            
            out = np.zeros(n, dtype=np.float32)
            if self.write_index >= n:
                out[:] = self.ring_buffer[self.write_index - n : self.write_index]
            else:
                part1_len = n - self.write_index
                part2_len = self.write_index
                out[:part1_len] = self.ring_buffer[-part1_len:]
                if part2_len > 0:
                    out[part1_len:] = self.ring_buffer[:part2_len]
            return out

    @staticmethod
    def get_devices():
        return sd.query_devices()

