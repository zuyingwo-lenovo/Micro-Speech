#include <windows.h>
#include <d3d11.h>
#include <tchar.h>
#include <vector>
#include <string>
#include <memory>
#include <deque>
#include <chrono>

// ImGui
#include "../../lib/imgui/imgui.h"
#include "../../lib/imgui/backends/imgui_impl_win32.h"
#include "../../lib/imgui/backends/imgui_impl_dx11.h"

// Project
#include "audio_capture.h"
#include "feature_extractor.h"
#include "tflite_handler.h"

// Data
static ID3D11Device*            g_pd3dDevice = nullptr;
static ID3D11DeviceContext*     g_pd3dDeviceContext = nullptr;
static IDXGISwapChain*          g_pSwapChain = nullptr;
static ID3D11RenderTargetView*  g_mainRenderTargetView = nullptr;

// Forward declarations
bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
void CreateRenderTarget();
void CleanupRenderTarget();
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// Application State
struct AppState {
    std::unique_ptr<AudioCapture> audio;
    std::unique_ptr<FeatureExtractor> features;
    std::unique_ptr<TFLiteHandler> tflite;

    bool is_running = false;
    float threshold = 0.70f;
    float cooldown = 2.0f;
    float current_score = 0.0f;
    std::string current_label = "---";
    std::deque<float> score_history;
    
    std::vector<AudioDeviceInfo> devices;
    int selected_device_idx = -1;
    
    struct LogEntry {
        std::string time;
        std::string message;
        ImVec4 color;
    };
    std::deque<LogEntry> logs;
    
    std::chrono::steady_clock::time_point last_trigger_time;
    bool trigger_active = false;

    void Log(const std::string& msg, ImVec4 color = ImVec4(1,1,1,1)) {
        auto now = std::chrono::system_clock::now();
        auto in_time_t = std::chrono::system_clock::to_time_t(now);
        char buf[16];
        struct tm time_info;
        localtime_s(&time_info, &in_time_t);
        strftime(buf, sizeof(buf), "%H:%M:%S", &time_info);
        
        logs.push_front({buf, msg, color});
        if (logs.size() > 100) logs.pop_back();
    }
};

static AppState g_app;

// ThinkPad Theme
void ApplyThinkPadTheme() {
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* colors = style.Colors;

    ImVec4 tp_red = ImVec4(0.85f, 0.11f, 0.11f, 1.00f);
    ImVec4 tp_black = ImVec4(0.08f, 0.08f, 0.08f, 1.00f);
    ImVec4 tp_dark_gray = ImVec4(0.15f, 0.15f, 0.15f, 1.00f);
    ImVec4 tp_light_gray = ImVec4(0.30f, 0.30f, 0.30f, 1.00f);

    colors[ImGuiCol_WindowBg]            = tp_black;
    colors[ImGuiCol_ChildBg]             = tp_dark_gray;
    colors[ImGuiCol_Header]               = tp_red;
    colors[ImGuiCol_HeaderHovered]        = ImVec4(1.0f, 0.2f, 0.2f, 1.0f);
    colors[ImGuiCol_HeaderActive]         = ImVec4(0.7f, 0.1f, 0.1f, 1.0f);
    colors[ImGuiCol_Button]               = tp_red;
    colors[ImGuiCol_ButtonHovered]        = ImVec4(1.0f, 0.2f, 0.2f, 1.0f);
    colors[ImGuiCol_ButtonActive]         = ImVec4(0.7f, 0.1f, 0.1f, 1.0f);
    colors[ImGuiCol_FrameBg]              = tp_dark_gray;
    colors[ImGuiCol_FrameBgHovered]       = tp_light_gray;
    colors[ImGuiCol_FrameBgActive]        = tp_red;
    colors[ImGuiCol_CheckMark]            = tp_red;
    colors[ImGuiCol_SliderGrab]           = tp_red;
    colors[ImGuiCol_SliderGrabActive]     = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
    colors[ImGuiCol_Text]                 = ImVec4(0.95f, 0.95f, 0.95f, 1.00f);
    colors[ImGuiCol_TitleBg]              = tp_black;
    colors[ImGuiCol_TitleBgActive]        = tp_black;
    colors[ImGuiCol_Separator]            = tp_light_gray;
    
    style.WindowRounding = 4.0f;
    style.FrameRounding = 2.0f;
    style.ScrollbarRounding = 0.0f;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR lpCmdLine, int nShowCmd)
{
    // Environment setup
    SetEnvironmentVariableA("TFLITE_XNNPACK_DELEGATE_DISABLED", "1");

    // Create application window
    WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(nullptr), nullptr, nullptr, nullptr, nullptr, L"HeyThinkPadClass", nullptr };
    ::RegisterClassExW(&wc);
    HWND hwnd = ::CreateWindowExW(0, L"HeyThinkPadClass", L"Hey ThinkPad - Native Dashboard", WS_OVERLAPPEDWINDOW, 100, 100, 800, 600, nullptr, nullptr, wc.hInstance, nullptr);

    // Initialize Direct3D
    if (!CreateDeviceD3D(hwnd)) {
        CleanupDeviceD3D();
        ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return 1;
    }

    ::ShowWindow(hwnd, SW_SHOWDEFAULT);
    ::UpdateWindow(hwnd);

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    
    ApplyThinkPadTheme();

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    // Initialize App Components
    g_app.audio = std::make_unique<AudioCapture>(16000, 2);
    g_app.features = std::make_unique<FeatureExtractor>(16000, 1024, 340, 40);
    g_app.tflite = std::make_unique<TFLiteHandler>();
    g_app.score_history.assign(100, 0.0f);

    // Initial Device List
    g_app.devices = AudioCapture::enumerate_devices();
    for (int i = 0; i < (int)g_app.devices.size(); ++i) {
        if (g_app.devices[i].is_default) {
            g_app.selected_device_idx = i;
            break;
        }
    }

    // Load Model
    std::string model_path = "artifacts/model.tflite";
    if (g_app.tflite->load_model(model_path)) {
        g_app.Log("Model loaded: " + model_path, ImVec4(0, 1, 0, 1));
    } else {
        g_app.Log("Error loading model: " + g_app.tflite->last_error, ImVec4(1, 0, 0, 1));
    }

    // Main loop
    bool done = false;
    while (!done)
    {
        MSG msg;
        while (::PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE))
        {
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
            if (msg.message == WM_QUIT)
                done = true;
        }
        if (done) break;

        // Process Logic
        if (g_app.is_running) {
            static auto last_process = std::chrono::steady_clock::now();
            auto now = std::chrono::steady_clock::now();
            if (std::chrono::duration_cast<std::chrono::milliseconds>(now - last_process).count() > 500) {
                last_process = now;
                
                auto audio_data = g_app.audio->get_last_n_samples(17000);
                auto feat_raw = g_app.features->compute_mel_spectrogram(audio_data);
                
                if (!feat_raw.empty()) {
                    auto feat = g_app.tflite->pad_or_trim(feat_raw);
                    float score = 0;
                    int label_idx = g_app.tflite->invoke(feat, &score);
                    
                    g_app.current_score = score;
                    g_app.score_history.push_front(score);
                    if (g_app.score_history.size() > 100) g_app.score_history.pop_back();

                    static const char* LABELS[] = { "SILENCE", "UNKNOWN", "HEY THINKPAD" };
                    if (label_idx >= 0 && label_idx < 3) g_app.current_label = LABELS[label_idx];

                    // Trigger check (index 2 = Hey ThinkPad)
                    if (label_idx == 2 && score >= g_app.threshold) {
                        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - g_app.last_trigger_time).count();
                        if (elapsed > g_app.cooldown) {
                            g_app.last_trigger_time = now;
                            g_app.trigger_active = true;
                            g_app.Log("! TRIGGER DETECTED ! (Score: " + std::to_string(score).substr(0, 4) + ")", ImVec4(1, 1, 0, 1));
                            MessageBeep(MB_OK);
                        }
                    }
                }
            }
            
            // Pulse trigger effect
            if (g_app.trigger_active) {
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - g_app.last_trigger_time).count();
                if (elapsed > 2000) g_app.trigger_active = false;
            }
        }

        // Start ImGui frame
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        // Main App Layout
        {
            ImGui::SetNextWindowPos(ImVec2(0, 0));
            ImGui::SetNextWindowSize(io.DisplaySize);
            ImGui::Begin("Dashboard", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus);

            // Header
            ImGui::TextColored(ImVec4(0.85f, 0.11f, 0.11f, 1.0f), "HEY THINKPAD");
            ImGui::SameLine();
            ImGui::Text(" | Native C++ Wake-Word Detection");
            ImGui::Separator();

            // Grid Layout
            if (ImGui::BeginTable("MainGrid", 2, ImGuiTableFlags_SizingStretchSame)) {
                ImGui::TableNextRow();
                
                // --- LEFT COLUMN: Controls ---
                ImGui::TableSetColumnIndex(0);
                ImGui::BeginChild("Controls", ImVec2(0, 0), true);
                ImGui::Text("ENGINE CONTROLS");
                ImGui::Separator();

                // Device Selection
                if (ImGui::BeginCombo("Input Device", g_app.selected_device_idx == -1 ? "Select Device..." : g_app.devices[g_app.selected_device_idx].name.c_str())) {
                    for (int n = 0; n < (int)g_app.devices.size(); n++) {
                        const bool is_selected = (g_app.selected_device_idx == n);
                        if (ImGui::Selectable(g_app.devices[n].name.c_str(), is_selected)) {
                            g_app.selected_device_idx = n;
                            if (g_app.is_running) {
                                g_app.audio->stop();
                                g_app.audio->start(&g_app.devices[n].id);
                                g_app.Log("Switched to device: " + g_app.devices[n].name);
                            }
                        }
                        if (is_selected) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }

                ImGui::SliderFloat("Threshold", &g_app.threshold, 0.0f, 1.0f, "%.2f");
                ImGui::SliderFloat("Cooldown (s)", &g_app.cooldown, 0.1f, 5.0f, "%.1f");

                ImGui::Spacing();
                ImGuiStyle& style = ImGui::GetStyle();
                if (g_app.is_running) {
                    if (ImGui::Button("STOP DETECTION", ImVec2(-FLT_MIN, 40))) {
                        g_app.audio->stop();
                        g_app.is_running = false;
                        g_app.Log("Detection stopped.");
                    }
                } else {
                    if (ImGui::Button("START DETECTION", ImVec2(-FLT_MIN, 40))) {
                        const ma_device_id* pID = (g_app.selected_device_idx >= 0) ? &g_app.devices[g_app.selected_device_idx].id : nullptr;
                        if (g_app.audio->start(pID)) {
                            g_app.is_running = true;
                            g_app.Log("Detection started.");
                        } else {
                            g_app.Log("Failed to start audio!", ImVec4(1, 0, 0, 1));
                        }
                    }
                }

                ImGui::EndChild();

                // --- RIGHT COLUMN: Monitoring ---
                ImGui::TableSetColumnIndex(1);
                ImGui::BeginChild("Monitoring", ImVec2(0, 0), true);
                ImGui::Text("LIVE MONITORING");
                ImGui::Separator();

                // Status Indicator
                ImVec4 status_color = g_app.trigger_active ? ImVec4(0, 1, 0, 1) : ImVec4(0.5, 0.5, 0.5, 1);
                ImGui::Text("Current Label: "); ImGui::SameLine();
                ImGui::TextColored(status_color, g_app.current_label.c_str());

                // Level Meter
                float level = g_app.audio->get_current_level() * 5.0f; // scaled for visibility
                ImGui::Text("Input Level");
                ImGui::ProgressBar(level, ImVec2(-FLT_MIN, 15), "");

                // Score Graph
                std::vector<float> graph_points(g_app.score_history.begin(), g_app.score_history.end());
                ImGui::PlotLines("Detection Confidence", graph_points.data(), (int)graph_points.size(), 0, nullptr, 0.0f, 1.0f, ImVec2(0, 80));

                // Trigger Pulse
                if (g_app.trigger_active) {
                    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0.2f, 0, 1));
                    ImGui::BeginChild("TriggerBanner", ImVec2(0, 50), true);
                    ImGui::SetWindowFontScale(1.5f);
                    ImGui::TextColored(ImVec4(0, 1, 0, 1), "  *** WAKE WORD DETECTED ***  ");
                    ImGui::EndChild();
                    ImGui::PopStyleColor();
                }

                ImGui::EndChild();
                ImGui::EndTable();
            }

            // Bottom Section: Logs
            ImGui::SetCursorPosY(ImGui::GetWindowHeight() - 180);
            ImGui::BeginChild("Logs", ImVec2(0, 160), true);
            ImGui::Text("EVENT LOGS");
            ImGui::Separator();
            for (auto& entry : g_app.logs) {
                ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "[%s]", entry.time.c_str());
                ImGui::SameLine();
                ImGui::TextColored(entry.color, "%s", entry.message.c_str());
            }
            ImGui::EndChild();

            ImGui::End();
        }

        // Rendering
        ImGui::Render();
        const float clear_color_with_alpha[4] = { 0.1f, 0.1f, 0.1f, 1.00f };
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear_color_with_alpha);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        g_pSwapChain->Present(1, 0); // Present with vsync
    }

    // Cleanup
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    CleanupDeviceD3D();
    ::DestroyWindow(hwnd);
    ::UnregisterClassW(wc.lpszClassName, wc.hInstance);

    return 0;
}

// Helper functions
bool CreateDeviceD3D(HWND hWnd)
{
    DXGI_SWAP_CHAIN_DESC sd;
    ZeroMemory(&sd, sizeof(sd));
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    UINT createDeviceFlags = 0;
    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevelArray[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0, };
    HRESULT res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createDeviceFlags, featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
    if (res != S_OK) return false;

    CreateRenderTarget();
    return true;
}

void CleanupDeviceD3D()
{
    CleanupRenderTarget();
    if (g_pSwapChain) { g_pSwapChain->Release(); g_pSwapChain = nullptr; }
    if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = nullptr; }
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
}

void CreateRenderTarget()
{
    ID3D11Texture2D* pBackBuffer;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
    pBackBuffer->Release();
}

void CleanupRenderTarget()
{
    if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = nullptr; }
}

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    switch (msg)
    {
    case WM_SIZE:
        if (g_pd3dDevice != nullptr && wParam != SIZE_MINIMIZED)
        {
            CleanupRenderTarget();
            g_pSwapChain->ResizeBuffers(0, (UINT)LOWORD(lParam), (UINT)HIWORD(lParam), DXGI_FORMAT_UNKNOWN, 0);
            CreateRenderTarget();
        }
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xFFF0) == SC_KEYMENU) // Disable ALT application menu
            return 0;
        break;
    case WM_DESTROY:
        ::PostQuitMessage(0);
        return 0;
    }
    return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}
