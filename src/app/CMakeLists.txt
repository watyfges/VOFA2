qt_policy(SET QTP0001 NEW)

add_library(vofa_app_controller STATIC
    AppController.cpp
    ImageProvider.cpp
    Logging.cpp
    SettingsStore.cpp
    TimePlotItem.cpp
    WaveformItem.cpp
)

target_compile_features(vofa_app_controller PUBLIC cxx_std_20)
target_link_libraries(vofa_app_controller PUBLIC
    Qt6::Core
    Qt6::Gui
    Qt6::Quick
    Qt6::Network
    Qt6::SerialPort
    vofa_core
    vofa_protocol
    vofa_commands
    vofa_data
    vofa_transport
)
target_include_directories(vofa_app_controller PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

if(WIN32)
    target_link_libraries(vofa_app_controller PUBLIC comdlg32)
endif()

qt_add_executable(vofa2_app
    main.cpp
)

if(WIN32)
    set_target_properties(vofa2_app PROPERTIES WIN32_EXECUTABLE TRUE)
    target_sources(vofa2_app PRIVATE app.rc)
    target_link_libraries(vofa2_app PRIVATE dbghelp)
endif()

qt_add_qml_module(vofa2_app
    URI Vofa2
    VERSION 1.0
    QML_FILES
        App.qml
)

target_link_libraries(vofa2_app PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Network
    Qt6::SerialPort
    vofa_app_controller
)
