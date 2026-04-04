#include <windows.h>
#include <string>
#include <vector>
#include <iostream>
#include <memory>
#include <cstdlib>
#include "audio_capture.h"
#include "feature_extractor.h"
#include "tflite_handler.h"

// Global objects
std::unique_ptr<AudioCapture> g_audio;
std::unique_ptr<FeatureExtractor> g_features;
std::unique_ptr<TFLiteHandler> g_tflite;

bool g_is_running = false;
float g_current_score = 0.0f;
std::string g_current_label = "---";
long long g_last_trigger_time = 0;

// Windows Callback
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            
            // Background color based on trigger
            HBRUSH hBrush;
            if (GetTickCount64() - g_last_trigger_time < 2000) {
                hBrush = CreateSolidBrush(RGB(0, 255, 0)); // Green
            } else {
                hBrush = CreateSolidBrush(RGB(240, 240, 240)); // Gray
            }
            FillRect(hdc, &ps.rcPaint, hBrush);
            DeleteObject(hBrush);

            SetBkMode(hdc, TRANSPARENT);
            std::string text = "Hey ThinkPad Detection\n\n";
            text += "Status: " + std::string(g_is_running ? "Running" : "Stopped") + "\n";
            text += "Label: " + g_current_label + "\n";
            text += "Score: " + std::to_string(g_current_score).substr(0, 4);
            
            RECT rect;
            GetClientRect(hwnd, &rect);
            DrawTextA(hdc, text.c_str(), -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    // Try to attach to the parent console to show logs in PowerShell/CMD
    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        FILE* fp;
        freopen_s(&fp, "CONOUT$", "w", stdout);
        freopen_s(&fp, "CONOUT$", "w", stderr);
        std::ios::sync_with_stdio();
    }

    // Disable XNNPACK globally for this process.
    // TFLite 2.14+ auto-enables XNNPACK, which fails on models with dynamic shape ops.
    // SetEnvironmentVariableA is the Win32 API and is more reliable for DLL visibility.
    SetEnvironmentVariableA("TFLITE_XNNPACK_DELEGATE_DISABLED", "1");

    // 1. Initialize Objects
    g_audio = std::make_unique<AudioCapture>(16000, 2);
    // FeatureExtractor params tuned to model input [1,40,47,1]:
    //   sample_rate=16000, fft_size=1024, hop_size=340 (~47 frames/sec), n_mels=40
    g_features = std::make_unique<FeatureExtractor>(16000, 1024, 340, 40);
    g_tflite = std::make_unique<TFLiteHandler>();

    // Model path: prefer command-line argument, fall back to exe-relative default.
    // Usage: HeyThinkPad.exe [path\to\model.tflite]
    std::string model_path;
    if (lpCmdLine && strlen(lpCmdLine) > 0) {
        // Strip surrounding quotes if present (drag-drop or shell quoting)
        model_path = lpCmdLine;
        if (!model_path.empty() && model_path.front() == '"') model_path = model_path.substr(1);
        if (!model_path.empty() && model_path.back()  == '"') model_path.pop_back();
    } else {
        // Default: resolve relative to the executable's own directory
        char exe_path[MAX_PATH] = {};
        GetModuleFileNameA(NULL, exe_path, MAX_PATH);
        model_path = exe_path;
        auto last_sep = model_path.find_last_of("\\/");
        if (last_sep != std::string::npos) model_path = model_path.substr(0, last_sep + 1);
        model_path += "artifacts\\model.tflite";
    }

    if (!g_tflite->load_model(model_path)) {
        std::string msg = "Failed to load model:\n" + model_path + "\n\n" + g_tflite->last_error;
        MessageBoxA(NULL, msg.c_str(), "Model Load Error", MB_ICONERROR);
        return 1;
    }


    // 2. Setup Win32 Window
    const char CLASS_NAME[] = "HeyThinkPadApp";
    WNDCLASSA wc = {};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    RegisterClassA(&wc);

    HWND hwnd = CreateWindowExA(
        0, CLASS_NAME, "Hey ThinkPad - C++ Native", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 400, 300, NULL, NULL, hInstance, NULL
    );

    if (hwnd == NULL) return 0;
    ShowWindow(hwnd, nCmdShow);

    // 3. Start Audio
    if (g_audio->start()) {
        g_is_running = true;
    } else {
        MessageBoxA(NULL, "Failed to start microphone.", "Error", MB_ICONERROR);
    }

    // 4. Main loop with processing
    MSG msg = {};
    while (msg.message != WM_QUIT) {
        if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        } else {
            // Process Audio Inference every 500ms
            static long long last_process = 0;
            long long now = GetTickCount64();
            if (now - last_process > 500) {
                last_process = now;

                // Model input: [1, 40, 47, 1] float32
                // Grab enough audio to produce ~47 frames at hop=340: 47*340+1024 ~= 17k samples
                auto audio_data = g_audio->get_last_n_samples(17000);
                auto feat_raw   = g_features->compute_mel_spectrogram(audio_data);

                if (!feat_raw.empty()) {
                    // Pad or trim to exact model input size
                    auto feat = g_tflite->pad_or_trim(feat_raw);

                    float score     = 0;
                    int   label_idx = g_tflite->invoke(feat, &score);

                    // Model output [1,3]: class 0=silence/background, 1=unknown, 2=hey_thinkpad
                    // (adjust indices to match your training label order)
                    static const char* LABELS[] = { "SILENCE", "UNKNOWN", "HEY THINKPAD" };
                    int n_labels = g_tflite->num_classes();

                    g_current_score = score;
                    if (label_idx >= 0 && label_idx < n_labels) {
                        g_current_label = LABELS[label_idx];
                    } else {
                        g_current_label = "?";
                    }

                    // Trigger on wake-word class (index 2) with confidence threshold
                    if (label_idx == 2 && score > 0.70f) {
                        g_last_trigger_time = now;
                    }
                    InvalidateRect(hwnd, NULL, TRUE);
                }
            }
            Sleep(50);
        }
    }

    return 0;
}
