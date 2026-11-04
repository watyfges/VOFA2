if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(WARNING "VOFA_BUILD_FUZZERS requires Clang; skipping fuzz targets")
    return()
endif()

foreach(target IN ITEMS justfloat firewater image sample_image)
    add_executable(fuzz_${target} fuzz_${target}.cpp)
    target_link_libraries(fuzz_${target} PRIVATE vofa_protocol)
    target_compile_options(fuzz_${target} PRIVATE
        -fsanitize=fuzzer,address,undefined
        -fno-omit-frame-pointer
        -g
    )
    target_link_options(fuzz_${target} PRIVATE
        -fsanitize=fuzzer,address,undefined
    )
endforeach()
