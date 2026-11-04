add_executable(vofa_tests
    test_main.cpp
    test_core.cpp
    test_protocol.cpp
    test_commands.cpp
    test_oneclick.cpp
    test_commandstore.cpp
    test_data.cpp
)

target_link_libraries(vofa_tests PRIVATE
    vofa_core
    vofa_protocol
    vofa_commands
    vofa_data
)

add_test(NAME vofa_tests COMMAND vofa_tests)
