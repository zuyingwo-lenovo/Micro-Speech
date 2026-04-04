import os
import sys
import tkinter as tk
from tkinter import ttk
from datetime import datetime

# Add src to Python Path
current_dir = os.path.dirname(os.path.abspath(__file__))
project_dir = os.path.dirname(current_dir)
sys.path.append(project_dir)

from src.live_inference import LiveInference
from src.audio_stream import AudioStream

MODEL_PATH = os.path.join(project_dir, "artifacts", "model.tflite")
LABELS_PATH = os.path.join(project_dir, "artifacts", "labels.txt")

class App:
    def __init__(self, root):
        self.root = root
        self.root.title("Hey ThinkPad - Wake Word Detection")
        self.root.geometry("400x500")
        
        self.inference = None
        
        self.setup_ui()
        self.load_model()
        
        self.is_running = False
        self.poll_interval = 250 # ms

    def load_model(self):
        try:
            self.inference = LiveInference(MODEL_PATH, LABELS_PATH)
            self.status_var.set(f"Model loaded: {os.path.basename(MODEL_PATH)}")
        except Exception as e:
            self.status_var.set(f"Error loading model: {e}")

    def setup_ui(self):
        main_frame = ttk.Frame(self.root, padding="10")
        main_frame.pack(fill=tk.BOTH, expand=True)
        
        # Audio Device
        ttk.Label(main_frame, text="Audio Device:").pack(anchor=tk.W)
        self.device_combo = ttk.Combobox(main_frame, state="readonly")
        try:
            devices = AudioStream.get_devices()
            input_devices = [f"{d['index']}: {d['name']}" for d in devices if d['max_input_channels'] > 0]
            self.device_combo['values'] = input_devices
            if input_devices:
                self.device_combo.current(0)
        except:
            self.device_combo['values'] = ["Default"]
            self.device_combo.current(0)
        self.device_combo.pack(fill=tk.X, pady=(0, 10))
        
        # Threshold
        ttk.Label(main_frame, text="Confidence Threshold:").pack(anchor=tk.W)
        self.thresh_scale = tk.Scale(main_frame, from_=0.0, to=1.0, resolution=0.05, orient=tk.HORIZONTAL)
        self.thresh_scale.set(0.7)
        self.thresh_scale.pack(fill=tk.X, pady=(0, 10))
        
        # Current Prediction
        self.pred_frame = tk.Frame(main_frame, bg="gray", height=100)
        self.pred_frame.pack(fill=tk.X, pady=10)
        self.pred_frame.pack_propagate(False)
        
        self.pred_label = tk.Label(self.pred_frame, text="---", font=("Helvetica", 24), bg="gray", fg="white")
        self.pred_label.pack(expand=True)
        
        # Controls
        self.btn_frame = ttk.Frame(main_frame)
        self.btn_frame.pack(fill=tk.X, pady=10)
        
        self.start_btn = ttk.Button(self.btn_frame, text="Start Detection", command=self.toggle_detection)
        self.start_btn.pack(side=tk.LEFT, expand=True, fill=tk.X, padx=5)
        
        # Log Box
        ttk.Label(main_frame, text="Event Logs:").pack(anchor=tk.W)
        self.log_text = tk.Text(main_frame, height=8, state=tk.DISABLED)
        self.log_text.pack(fill=tk.BOTH, expand=True)
        
        # Status
        self.status_var = tk.StringVar()
        self.status_var.set("Ready")
        ttk.Label(main_frame, textvariable=self.status_var, relief=tk.SUNKEN, anchor=tk.W).pack(fill=tk.X, side=tk.BOTTOM)

    def log(self, message):
        self.log_text.config(state=tk.NORMAL)
        self.log_text.insert(tk.END, f"[{datetime.now().strftime('%H:%M:%S')}] {message}\n")
        self.log_text.see(tk.END)
        self.log_text.config(state=tk.DISABLED)

    def toggle_detection(self):
        if not self.inference:
            self.log("Model not loaded!")
            return
            
        if self.is_running:
            self.inference.stop_stream()
            self.is_running = False
            self.start_btn.config(text="Start Detection")
            self.log("Detection stopped.")
            self.pred_label.config(text="---", bg="gray")
            self.pred_frame.config(bg="gray")
        else:
            dev_str = self.device_combo.get()
            dev_id = None
            if ":" in dev_str:
                dev_id = int(dev_str.split(":")[0])
                
            try:
                self.inference.start_stream(device_id=dev_id)
                self.is_running = True
                self.start_btn.config(text="Stop Detection")
                self.log("Detection started.")
                self.root.after(self.poll_interval, self.poll_inference)
            except Exception as e:
                self.log(f"Error starting audio: {e}")

    def poll_inference(self):
        if not self.is_running:
            return
            
        try:
            threshold = self.thresh_scale.get()
            label, score, is_trigger = self.inference.process_frame(threshold=threshold)
            
            if label is not None:
                lbl_text = f"{label} ({score:.2f})"
                self.pred_label.config(text=lbl_text)
                
                if label == "yes" and score >= threshold:
                    self.pred_label.config(bg="green")
                    self.pred_frame.config(bg="green")
                else:
                    self.pred_label.config(bg="gray")
                    self.pred_frame.config(bg="gray")
                    
                if is_trigger:
                    self.log(f"! WAKE WORD DETECTED ! ({score:.2f})")
                    print("\a", end="") # system beep if possible
        except Exception as e:
            self.log(f"Inference error: {e}")
            
        if self.is_running:
            self.root.after(self.poll_interval, self.poll_inference)

if __name__ == "__main__":
    root = tk.Tk()
    app = App(root)
    root.mainloop()
