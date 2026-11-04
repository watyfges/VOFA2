add_library(vofa_remote STATIC
    RemoteProtocol.cpp
    RemoteControlServer.cpp
    WebSocketConnection.cpp
)

target_compile_features(vofa_remote PUBLIC cxx_std_20)
target_link_libraries(vofa_remote PUBLIC
    Qt6::Core
    Qt6::Network
)
target_include_directories(vofa_remote PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)
