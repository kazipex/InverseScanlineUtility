cmake_minimum_required(VERSION 3.16)
project(InverseScanlineUtility LANGUAGES CXX)

if (NOT WIN32)
    message(FATAL_ERROR "This project uses Win32/DXGI/Direct3D 11 APIs and only builds on Windows.")
endif()

# --- Dear ImGui, for the control-panel GUI ---------------------------------
# Pulled from GitHub at configure time rather than vendored, so the project
# stays a small, easy-to-read tree. This does mean `cmake -B build ...` needs
# internet access the first time (and re-downloads if you delete build/).
include(FetchContent)
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.91.0
)
FetchContent_MakeAvailable(imgui)

# --- nlohmann/json, for profile export/import ------------------------------
# Header-only, so this just adds an include path — no extra .cpp files to
# compile. JSON_BuildTests is turned off so configuring doesn't also pull in
# and configure that library's own test suite, which we never use.
set(JSON_BuildTests OFF CACHE INTERNAL "")
FetchContent_Declare(
    json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
)
FetchContent_MakeAvailable(json)

add_executable(InverseScanlineUtility WIN32
    main.cpp
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp
)

target_include_directories(InverseScanlineUtility PRIVATE
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)

set_property(TARGET InverseScanlineUtility PROPERTY CXX_STANDARD 17)

# main.cpp uses wide (L"...") string literals with the CreateWindowEx/
# WNDCLASSEX Win32 APIs, which only match the *W (Unicode) function
# variants. Without these defines MSVC defaults to the ANSI *A variants,
# which expect char* and fail to compile against L"..." literals.
target_compile_definitions(InverseScanlineUtility PRIVATE UNICODE _UNICODE)

target_link_libraries(InverseScanlineUtility
    d3d11.lib
    dxgi.lib
    d3dcompiler.lib
    user32.lib
    comdlg32.lib   # GetOpenFileNameW/GetSaveFileNameW, for profile import/export dialogs
    shell32.lib    # Shell_NotifyIcon, for the minimize-to-tray icon
    nlohmann_json::nlohmann_json
)
