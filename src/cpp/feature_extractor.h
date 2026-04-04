#ifndef FEATURE_EXTRACTOR_H
#define FEATURE_EXTRACTOR_H

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <vector>
#include <complex>
#include <cmath>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

class FeatureExtractor {
public:
    FeatureExtractor(int sample_rate = 16000, int n_fft = 1024, int hop_length = 512, int n_mels = 40)
        : sample_rate(sample_rate), n_fft(n_fft), hop_length(hop_length), n_mels(n_mels) {
        init_mel_filterbank();
        init_window();
    }

    // Input: interleaved mono audio
    // Output: log-mel spectrogram as a flat vector (n_mels * n_frames)
    std::vector<float> compute_mel_spectrogram(const std::vector<float>& audio) {
        int n_samples = (int)audio.size();
        int n_frames = 1 + (n_samples - n_fft) / hop_length;
        if (n_frames <= 0) return {};

        std::vector<float> spectrogram;
        spectrogram.reserve(n_mels * n_frames);

        for (int f = 0; f < n_frames; ++f) {
            std::vector<std::complex<double>> frame(n_fft);
            for (int i = 0; i < n_fft; ++i) {
                frame[i] = std::complex<double>(audio[f * hop_length + i] * window[i], 0.0);
            }

            fft(frame);

            // Compute power spectrum
            std::vector<double> power_spectrum(n_fft / 2 + 1);
            for (int i = 0; i <= n_fft / 2; ++i) {
                power_spectrum[i] = std::norm(frame[i]);
            }

            // Apply mel filters
            for (int m = 0; m < n_mels; ++m) {
                double mel_energy = 0;
                for (int i = 0; i < (int)power_spectrum.size(); ++i) {
                    mel_energy += power_spectrum[i] * mel_filters[m][i];
                }
                // Log scale (with small epsilon)
                float log_mel = (float)(10.0 * std::log10((std::max)(mel_energy, 1.0e-10)));
                spectrogram.push_back(log_mel);
            }
        }

        return spectrogram;
    }

private:
    int sample_rate;
    int n_fft;
    int hop_length;
    int n_mels;
    std::vector<std::vector<double>> mel_filters;
    std::vector<double> window;

    void init_window() {
        window.resize(n_fft);
        for (int i = 0; i < n_fft; ++i) {
            // Hann window
            window[i] = 0.5 * (1.0 - std::cos(2.0 * M_PI * i / (n_fft - 1)));
        }
    }

    double hz_to_mel(double hz) {
        return 2595.0 * std::log10(1.0 + hz / 700.0);
    }

    double mel_to_hz(double mel) {
        return 700.0 * (std::pow(10.0, mel / 2595.0) - 1.0);
    }

    void init_mel_filterbank() {
        double f_min = 0;
        double f_max = sample_rate / 2.0;
        double mel_min = hz_to_mel(f_min);
        double mel_max = hz_to_mel(f_max);

        std::vector<double> mel_points(n_mels + 2);
        for (int i = 0; i < n_mels + 2; ++i) {
            mel_points[i] = mel_min + i * (mel_max - mel_min) / (n_mels + 1);
        }

        std::vector<int> bin_points(n_mels + 2);
        for (int i = 0; i < n_mels + 2; ++i) {
            bin_points[i] = (int)std::floor((n_fft + 1) * mel_to_hz(mel_points[i]) / sample_rate);
        }

        mel_filters.assign(n_mels, std::vector<double>(n_fft / 2 + 1, 0.0));
        for (int m = 1; m <= n_mels; ++m) {
            for (int i = bin_points[m - 1]; i < bin_points[m]; ++i) {
                mel_filters[m - 1][i] = (double)(i - bin_points[m - 1]) / (bin_points[m] - bin_points[m - 1]);
            }
            for (int i = bin_points[m]; i < bin_points[m + 1]; ++i) {
                mel_filters[m - 1][i] = (double)(bin_points[m + 1] - i) / (bin_points[m + 1] - bin_points[m]);
            }
        }
    }

    // Basic Iterative Cooley-Tukey FFT
    void fft(std::vector<std::complex<double>>& a) {
        int n = (int)a.size();
        for (int i = 1, j = 0; i < n; i++) {
            int bit = n >> 1;
            for (; j & bit; bit >>= 1) j ^= bit;
            j ^= bit;
            if (i < j) std::swap(a[i], a[j]);
        }
        for (int len = 2; len <= n; len <<= 1) {
            double ang = 2.0 * M_PI / len;
            std::complex<double> wlen(std::cos(ang), std::sin(ang));
            for (int i = 0; i < n; i += len) {
                std::complex<double> w(1);
                for (int j = 0; j < len / 2; j++) {
                    std::complex<double> u = a[i + j], v = a[i + j + len / 2] * w;
                    a[i + j] = u + v;
                    a[i + j + len / 2] = u - v;
                    w *= wlen;
                }
            }
        }
    }
};

#endif
