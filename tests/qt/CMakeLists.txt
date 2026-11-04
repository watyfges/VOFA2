add_executable(vofa_transport_tests
    test_transport.cpp
)

target_link_libraries(vofa_transport_tests PRIVATE
    Qt6::Test
    Qt6::Core
    Qt6::Network
    Qt6::SerialPort
    vofa_transport
    vofa_data
    vofa_protocol
)

add_test(NAME vofa_transport_tests COMMAND vofa_transport_tests)

add_executable(vofa_app_controller_tests
    test_app_controller.cpp
)

target_link_libraries(vofa_app_controller_tests PRIVATE
    Qt6::Test
    Qt6::Core
    Qt6::Network
    Qt6::SerialPort
    vofa_app_controller
)

add_test(NAME vofa_app_controller_tests COMMAND vofa_app_controller_tests)
