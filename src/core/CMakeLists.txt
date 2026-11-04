add_library(vofa_core INTERFACE)

target_compile_features(vofa_core INTERFACE cxx_std_20)
target_include_directories(vofa_core INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)
