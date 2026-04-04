#include "tensorflow/lite/core/c/c_api.h"
#include <stdio.h>

int main() {
    TfLiteTensor* tensor = nullptr;
    // Try to call the function
    size_t size = TfLiteTensorByteSize(tensor);
    printf("Size: %zu\n", size);
    return 0;
}
