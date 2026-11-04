add_library(vofa_protocol STATIC
    FireWaterParser.cpp
    ImageParser.cpp
    JustFloatParser.cpp
    ParserFactory.cpp
    SampleImageParser.cpp
    RawParser.cpp
)

target_compile_features(vofa_protocol PUBLIC cxx_std_20)
target_link_libraries(vofa_protocol PUBLIC vofa_core)
target_include_directories(vofa_protocol PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)
