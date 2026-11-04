add_library(vofa_transport STATIC
    TransportController.cpp
    TransportWorker.cpp
)

target_compile_features(vofa_transport PUBLIC cxx_std_20)
target_link_libraries(vofa_transport PUBLIC
    Qt6::Core
    Qt6::Network
    Qt6::SerialPort
    vofa_core
    vofa_protocol
    vofa_data
)
target_include_directories(vofa_transport PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)
