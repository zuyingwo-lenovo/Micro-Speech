#ifndef AUDIO_CAPTURE_H
#define AUDIO_CAPTURE_H

#define MINIAUDIO_IMPLEMENTATION
#include "../../lib/miniaudio.h"
#include <vector>
#include <mutex>
#include <iostream>

class AudioCapture {
public:
    AudioCapture(int sample_rate = 16000, int buffer_size_sec = 2) 
        : sample_rate(sample_rate), buffer_size_samples(sample_rate * buffer_size_sec), write_pos(0) {
        ring_buffer.resize(buffer_size_samples, 0);
    }

    ~AudioCapture() {
        stop();
    }

    bool start() {
        ma_device_config config = ma_device_config_init(ma_device_type_capture);
        config.capture.format = ma_format_f32;
        config.capture.channels = 1;
        config.sampleRate = sample_rate;
        config.dataCallback = data_callback;
        config.pUserData = this;

        if (ma_device_init(NULL, &config, &device) != MA_SUCCESS) {
            std::cerr << "Failed to initialize capture device." << std::endl;
            return false;
        }

        if (ma_device_start(&device) != MA_SUCCESS) {
            std::cerr << "Failed to start capture device." << std::endl;
            ma_device_uninit(&device);
            return false;
        }

        return true;
    }

    void stop() {
        ma_device_stop(&device);
        ma_device_uninit(&device);
    }

    // Get last N samples
    std::vector<float> get_last_n_samples(int n) {
        std::lock_guard<std::mutex> lock(buffer_mutex);
        std::vector<float> out(n);
        int read_pos = (write_pos - n + buffer_size_samples) % buffer_size_samples;

        for (int i = 0; i < n; ++i) {
            out[i] = ring_buffer[(read_pos + i) % buffer_size_samples];
        }
        return out;
    }

private:
    int sample_rate;
    int buffer_size_samples;
    std::vector<float> ring_buffer;
    int write_pos;
    ma_device device;
    std::mutex buffer_mutex;

    static void data_callback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount) {
        AudioCapture* pCapture = (AudioCapture*)pDevice->pUserData;
        if (!pCapture || !pInput) return;

        const float* pInputF32 = (const float*)pInput;
        std::lock_guard<std::mutex> lock(pCapture->buffer_mutex);

        for (ma_uint32 i = 0; i < frameCount; ++i) {
            pCapture->ring_buffer[pCapture->write_pos] = pInputF32[i];
            pCapture->write_pos = (pCapture->write_pos + 1) % pCapture->buffer_size_samples;
        }
    }
};

#endif
