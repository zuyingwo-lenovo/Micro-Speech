cmake_minimum_required(VERSION 3.10)
project(HeyThinkPadNative)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)

# 1. Global Definitions
add_definitions(-DNOMINMAX)

# 2. Executable
add_executable(HeyThinkPad WIN32 
    src/cpp/main_win32.cpp
)

# 3. Include Directories
target_include_directories(HeyThinkPad PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/tflite/include
)

# 4. TFLite Libraries
set(TFLITE_LIB "${CMAKE_CURRENT_SOURCE_DIR}/lib/tflite/lib/tensorflowlite_c.lib")
set(TFLITE_DLL "${CMAKE_CURRENT_SOURCE_DIR}/lib/tflite/bin/tensorflowlite_c.dll")

# 5. Link Libraries
target_link_libraries(HeyThinkPad 
    PRIVATE 
    ${TFLITE_LIB}
    winmm.lib
    ole32.lib
    user32.lib
    gdi32.lib
)

# 6. Copy DLL to output directory
add_custom_command(TARGET HeyThinkPad POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${TFLITE_DLL}
    $<TARGET_FILE_DIR:HeyThinkPad>
)

# 7. Copy model to output directory for convenience
add_custom_command(TARGET HeyThinkPad POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:HeyThinkPad>/artifacts
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${CMAKE_CURRENT_SOURCE_DIR}/artifacts/model.tflite
    $<TARGET_FILE_DIR:HeyThinkPad>/artifacts/model.tflite
)
