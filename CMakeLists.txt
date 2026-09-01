cmake_minimum_required(VERSION 3.24)

project(NMRiH2OfflineDebugOverlay LANGUAGES C CXX)

if(NOT MSVC)
    message(FATAL_ERROR "This project targets 64-bit Windows and requires MSVC.")
endif()

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Configure an x64 build. The supplied NMRiH2 SDK is 64-bit.")
endif()

set(SDK_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/CppSDK")
set(IMGUI_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui")
set(MINHOOK_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/minhook")

set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build dependencies as static libraries" FORCE)

add_library(minhook STATIC
    "${MINHOOK_ROOT}/src/buffer.c"
    "${MINHOOK_ROOT}/src/hook.c"
    "${MINHOOK_ROOT}/src/trampoline.c"
    "${MINHOOK_ROOT}/src/hde/hde64.c"
)
target_include_directories(minhook PUBLIC "${MINHOOK_ROOT}/include")
target_include_directories(minhook PRIVATE
    "${MINHOOK_ROOT}/src"
    "${MINHOOK_ROOT}/src/hde"
)

add_library(dear_imgui STATIC
    "${IMGUI_ROOT}/imgui.cpp"
    "${IMGUI_ROOT}/imgui_draw.cpp"
    "${IMGUI_ROOT}/imgui_tables.cpp"
    "${IMGUI_ROOT}/imgui_widgets.cpp"
    "${IMGUI_ROOT}/backends/imgui_impl_win32.cpp"
    "${IMGUI_ROOT}/backends/imgui_impl_dx11.cpp"
    "${IMGUI_ROOT}/backends/imgui_impl_dx12.cpp"
)
target_include_directories(dear_imgui PUBLIC
    "${IMGUI_ROOT}"
    "${IMGUI_ROOT}/backends"
)
target_compile_features(dear_imgui PUBLIC cxx_std_20)
target_compile_definitions(dear_imgui PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    UNICODE
    _UNICODE
)
target_link_libraries(dear_imgui PUBLIC d3d11 d3d12 dxgi)

add_library(NMRiH2_Offline_DebugOverlay SHARED
    src/dllmain.cpp
    "${SDK_ROOT}/SDK/Basic.cpp"
    "${SDK_ROOT}/SDK/CoreUObject_functions.cpp"
    "${SDK_ROOT}/SDK/Engine_functions.cpp"
    "${SDK_ROOT}/SDK/Kosovo_functions.cpp"
)

target_include_directories(NMRiH2_Offline_DebugOverlay PRIVATE "${SDK_ROOT}")
target_compile_features(NMRiH2_Offline_DebugOverlay PRIVATE cxx_std_20)
target_link_libraries(NMRiH2_Offline_DebugOverlay PRIVATE
    dear_imgui
    minhook
    d3d11
    d3d12
    dxgi
    shell32
)

target_compile_definitions(NMRiH2_Offline_DebugOverlay PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    UNICODE
    _UNICODE
)

target_compile_options(NMRiH2_Offline_DebugOverlay PRIVATE
    /MP
    /bigobj
    /permissive-
    /utf-8
    /Zc:__cplusplus
)

set_target_properties(NMRiH2_Offline_DebugOverlay PROPERTIES
    OUTPUT_NAME "NMRiH2_Offline_DebugOverlay"
)
