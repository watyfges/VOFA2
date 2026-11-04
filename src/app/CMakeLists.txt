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
    vofa_remote
    vofa_transport
)
target_include_directories(vofa_app_controller PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

if(WIN32)
    target_link_libraries(vofa_app_controller PUBLIC comdlg32)
endif()

# 版本号统一来自 -DVOFA_VERSION（CI 每次构建递增），从这里流向 C++ / QML / exe 资源。
target_compile_definitions(vofa_app_controller PUBLIC VOFA_VERSION_STRING="${VOFA_VERSION}")

qt_add_executable(vofa2_app
    main.cpp
)

if(WIN32)
    set_target_properties(vofa2_app PROPERTIES WIN32_EXECUTABLE TRUE)
    target_sources(vofa2_app PRIVATE app.rc)
    target_link_libraries(vofa2_app PRIVATE dbghelp)

    # Exe 的「属性 → 详细信息」里的文件/产品版本。
    # version.rc.in 是 UTF-8，MinGW 的 windres 默认按 cp1252 解码，不加 -c 65001
    # 中文描述会变成乱码。
    if(MINGW)
        set(CMAKE_RC_FLAGS "${CMAKE_RC_FLAGS} -c 65001")
    endif()
    string(REPLACE "." "," VOFA_VERSION_COMMA "${VOFA_VERSION}")
    set(VOFA_VERSION_COMMA "${VOFA_VERSION_COMMA},0")
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/version.rc.in"
        "${CMAKE_CURRENT_BINARY_DIR}/version.rc"
        @ONLY
    )
    target_sources(vofa2_app PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/version.rc")
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
