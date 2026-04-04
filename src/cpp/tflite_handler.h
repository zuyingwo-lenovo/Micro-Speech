#ifndef TFLITE_HANDLER_H
#define TFLITE_HANDLER_H

#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <windows.h>  // for OutputDebugStringA
#include <cstdarg>
#include <cstdio>
#include <iostream>
#include <fstream>
#include <malloc.h>

#include "tensorflow/lite/core/c/c_api.h"
#include "tensorflow/lite/core/c/c_api_types.h"
#include "tensorflow/lite/core/c/c_api_experimental.h"
#include "tensorflow/lite/core/c/common.h"

// Helper: emit message to debugger, console and local log file
static void tflite_log(const std::string& msg) {
  OutputDebugStringA(("[TFLite] " + msg + "\n").c_str());
  std::cout << "[TFLite] " << msg << std::endl;
  
  static std::ofstream log_file("tflite_debug.log", std::ios::app);
  if (log_file.is_open()) {
    log_file << "[TFLite] " << msg << std::endl;
    log_file.flush();
  }
}

class TFLiteHandler {
public:
  TFLiteHandler() : model(nullptr), interpreter(nullptr), model_data_(nullptr) {
    if (g_instance == nullptr) g_instance = this;
  }
  ~TFLiteHandler() {
    if (interpreter) TfLiteInterpreterDelete(interpreter);
    if (model)       TfLiteModelDelete(model);
    if (model_data_) _aligned_free(model_data_);
    if (g_instance == this) g_instance = nullptr;
  }

  // Static reporter callback
  static void reporter(void* user_data, const char* format, va_list args) {
    char buf[1024];
    vsnprintf(buf, sizeof(buf), format, args);
    tflite_log("Runtime Error: " + std::string(buf));
    if (g_instance) {
      if (!g_instance->last_error.empty()) g_instance->last_error += "\n";
      g_instance->last_error += buf;
    }
  }

  static TFLiteHandler* g_instance;

  // Returns a human-readable diagnostic string (empty = OK).
  std::string last_error;

  // Load model by reading file bytes in C++ and calling TfLiteModelCreate.
  // This avoids any path/encoding issues in TfLiteModelCreateFromFile on Windows.
  bool load_model(const std::string& model_path) {
    tflite_log("TFLite Version: " + std::string(TfLiteVersion()));
    tflite_log("Loading model: " + model_path);

    // Read file into memory
    std::ifstream ifs(model_path, std::ios::binary | std::ios::ate);
    if (!ifs) {
      last_error = "Cannot open file: " + model_path;
      tflite_log(last_error);
      return false;
    }
    auto sz = (size_t)ifs.tellg();
    ifs.seekg(0);
    
    if (model_data_) _aligned_free(model_data_);
    model_data_ = (uint8_t*)_aligned_malloc(sz, 16);
    if (!model_data_) {
      last_error = "Failed to allocate 16-byte aligned memory";
      tflite_log(last_error);
      return false;
    }

    if (!ifs.read(reinterpret_cast<char*>(model_data_), sz)) {
      last_error = "Failed to read file: " + model_path;
      tflite_log(last_error);
      return false;
    }
    tflite_log("Read " + std::to_string(sz) + " bytes to aligned address: " + std::to_string((size_t)model_data_));
 
    // Create model from buffer (keeps a reference — model_data_ must stay alive)
    model = TfLiteModelCreate(model_data_, sz);
    if (!model) {
      last_error = "TfLiteModelCreate failed (bad flatbuffer?)";
      tflite_log(last_error);
      return false;
    }

    TfLiteInterpreterOptions* options = TfLiteInterpreterOptionsCreate();
    TfLiteInterpreterOptionsSetNumThreads(options, 2);
    TfLiteInterpreterOptionsSetErrorReporter(options, reporter, nullptr);
    
    // XNNPACK is disabled via the TFLITE_XNNPACK_DELEGATE_DISABLED environment 
    // variable in main_win32.cpp, as this distribution lacks the C API setter.

    interpreter = TfLiteInterpreterCreate(model, options);
    TfLiteInterpreterOptionsDelete(options);

    if (!interpreter) {
      last_error = "TfLiteInterpreterCreate failed";
      tflite_log(last_error);
      return false;
    }

    if (TfLiteInterpreterAllocateTensors(interpreter) != kTfLiteOk) {
      last_error = "TfLiteInterpreterAllocateTensors failed";
      tflite_log(last_error);
      return false;
    }

    // Log confirmed tensor shapes
    auto* in  = TfLiteInterpreterGetInputTensor(interpreter, 0);
    auto* out = TfLiteInterpreterGetOutputTensor(interpreter, 0);
    int in_rank  = TfLiteTensorNumDims(in);
    int out_rank = TfLiteTensorNumDims(out);

    std::string in_shape = "[";
    for (int i = 0; i < in_rank;  ++i) in_shape  += std::to_string(TfLiteTensorDim(in,  i)) + (i+1<in_rank  ? "," : "]");
    std::string out_shape = "[";
    for (int i = 0; i < out_rank; ++i) out_shape += std::to_string(TfLiteTensorDim(out, i)) + (i+1<out_rank ? "," : "]");

    input_elements_  = TfLiteTensorByteSize(in) / sizeof(float);
    output_elements_ = TfLiteTensorByteSize(out) / sizeof(float);

    tflite_log("Input  tensor: " + in_shape  + "  bytes=" + std::to_string(TfLiteTensorByteSize(in)));
    tflite_log("Output tensor: " + out_shape + "  bytes=" + std::to_string(TfLiteTensorByteSize(out)));
    tflite_log("Model loaded OK");
    return true;
  }

  // Returns the number of output classes (after load_model succeeds).
  int num_classes() const { return (int)output_elements_; }

  // Input:  flat float32 array of length = product of all input dims.
  //         Caller must ensure features.size() == input_elements_
  //         (use pad_or_trim() below to be safe).
  // Output: index of highest-scoring class; *out_score filled if non-null.
  int invoke(const std::vector<float>& features, float* out_score) {
    TfLiteTensor* input_tensor  = TfLiteInterpreterGetInputTensor(interpreter, 0);
    size_t        expected_bytes = TfLiteTensorByteSize(input_tensor);
    size_t        provided_bytes = features.size() * sizeof(float);

    if (provided_bytes != expected_bytes) {
      tflite_log("invoke: size mismatch — expected " + std::to_string(expected_bytes) +
                 " bytes, got " + std::to_string(provided_bytes));
      // Still attempt inference with what we have (zero-pad or truncate)
    }

    void*  dst  = TfLiteTensorData(input_tensor);
    size_t copy = std::min(provided_bytes, expected_bytes);
    if (provided_bytes < expected_bytes)
      memset(dst, 0, expected_bytes);             // zero-pad
    memcpy(dst, features.data(), copy);           // copy available data

    if (TfLiteInterpreterInvoke(interpreter) != kTfLiteOk) {
      tflite_log("TfLiteInterpreterInvoke failed");
      return -1;
    }

    const TfLiteTensor* output_tensor = TfLiteInterpreterGetOutputTensor(interpreter, 0);
    const float*        scores        = (const float*)TfLiteTensorData(output_tensor);
    int                 n             = (int)output_elements_;

    int   max_idx   = 0;
    float max_score = scores[0];
    for (int i = 1; i < n; ++i) {
      if (scores[i] > max_score) { max_score = scores[i]; max_idx = i; }
    }

    if (out_score) *out_score = max_score;
    return max_idx;
  }

  // Utility: resize features to exactly input_elements_ (pad with 0 or truncate).
  std::vector<float> pad_or_trim(const std::vector<float>& in) const {
    std::vector<float> out(input_elements_, 0.0f);
    size_t n = std::min(in.size(), input_elements_);
    std::copy(in.begin(), in.begin() + n, out.begin());
    return out;
  }

  size_t input_elements()  const { return input_elements_;  }

private:
  TfLiteModel*       model       = nullptr;
  TfLiteInterpreter* interpreter = nullptr;
  uint8_t*           model_data_ = nullptr;   // 16-byte aligned backing buffer
  size_t input_elements_  = 0;
  size_t output_elements_ = 0;
};

// Define static instance for the reporter
TFLiteHandler* TFLiteHandler::g_instance = nullptr;

#endif
