add_library(vofa_data STATIC
    ChannelStore.cpp
)

target_compile_features(vofa_data PUBLIC cxx_std_20)
target_link_libraries(vofa_data PUBLIC vofa_core)
target_include_directories(vofa_data PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)
