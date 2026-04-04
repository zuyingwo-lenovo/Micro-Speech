cmake_minimum_required(VERSION 3.10)
project(HeyThinkPadNative)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)

# 1. Global Definitions
add_definitions(-DNOMINMAX)

# 2. ImGui Sources
set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/lib/imgui)
set(IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/backends/imgui_impl_win32.cpp
    ${IMGUI_DIR}/backends/imgui_impl_dx11.cpp
)

# 3. Executable
add_executable(HeyThinkPad WIN32 
    src/cpp/main_win32.cpp
    ${IMGUI_SOURCES}
)

# 4. Include Directories
target_include_directories(HeyThinkPad PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/tflite/include
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
)

# 5. TFLite Libraries
set(TFLITE_LIB "${CMAKE_CURRENT_SOURCE_DIR}/lib/tflite/lib/tensorflowlite_c.lib")
set(TFLITE_DLL "${CMAKE_CURRENT_SOURCE_DIR}/lib/tflite/bin/tensorflowlite_c.dll")

# 6. Link Libraries
target_link_libraries(HeyThinkPad 
    PRIVATE 
    ${TFLITE_LIB}
    winmm.lib
    ole32.lib
    user32.lib
    gdi32.lib
    d3d11.lib
    dxgi.lib
    d3dcompiler.lib
)

# 7. Copy DLL to output directory
add_custom_command(TARGET HeyThinkPad POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${TFLITE_DLL}
    $<TARGET_FILE_DIR:HeyThinkPad>
)

# 8. Copy model to output directory
add_custom_command(TARGET HeyThinkPad POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:HeyThinkPad>/artifacts
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${CMAKE_CURRENT_SOURCE_DIR}/artifacts/model.tflite
    $<TARGET_FILE_DIR:HeyThinkPad>/artifacts/model.tflite
)
