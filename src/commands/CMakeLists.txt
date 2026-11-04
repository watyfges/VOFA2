add_library(vofa_commands STATIC
    CommandStore.cpp
    OneClickSend.cpp
    TemplateFormatter.cpp
)

target_compile_features(vofa_commands PUBLIC cxx_std_20)
target_include_directories(vofa_commands PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)
