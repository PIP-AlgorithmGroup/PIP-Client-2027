cmake_minimum_required(VERSION 3.25)

include(cmake/version.cmake)
project(PIPClient VERSION ${PIP_VERSION_BASE} LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 两平台固定同一 Qt 基线；拒绝旧 kit，防止 IDE 缓存悄悄选回已退役版本。
find_package(Qt6 6.12.0 EXACT REQUIRED COMPONENTS Quick QuickControls2 Svg Network)
# ShaderTools 仅构建时烘焙跨 RHI shader；运行时不增加编译器/厂商 SDK 依赖。
find_package(Qt6ShaderTools ${Qt6_VERSION} EXACT REQUIRED)
find_package(Qt6GuiPrivate ${Qt6_VERSION} EXACT REQUIRED)
# SerialPort 可随 Qt kit 安装，也可用同版独立前缀；配置/构建不联网下载依赖。
set(PIP_SERIALPORT_ROOT "" CACHE PATH "Qt SerialPort installation prefix")
if (PIP_SERIALPORT_ROOT)
    list(PREPEND CMAKE_PREFIX_PATH "${PIP_SERIALPORT_ROOT}")
endif()
find_package(Qt6SerialPort ${Qt6_VERSION} EXACT REQUIRED
    HINTS "${PIP_SERIALPORT_ROOT}/lib/cmake/Qt6SerialPort")
include(cmake/ffmpeg.cmake)

# Use the Qt MQTT module matching the selected Qt kit; no network fetch during app builds.
set(PIP_MQTT_ROOT "" CACHE PATH "Qt MQTT installation prefix")
if (PIP_MQTT_ROOT)
    list(PREPEND CMAKE_PREFIX_PATH "${PIP_MQTT_ROOT}")
endif()
find_package(Qt6Mqtt ${Qt6_VERSION} EXACT REQUIRED
    HINTS "${PIP_MQTT_ROOT}/lib/cmake/Qt6Mqtt")

# Local dependency prefixes are grouped by target OS; compiler ABI must match the selected kit.
# CI and externally installed SDKs override PIP_PROTOBUF_ROOT without changing source paths.
set(PIP_PROTOBUF_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/installed/${CMAKE_SYSTEM_NAME}/protobuf-3.20.3"
    CACHE PATH "PIP-Client Protobuf installation prefix")
if (EXISTS "${PIP_PROTOBUF_ROOT}/lib/cmake/protobuf/protobuf-config.cmake")
    list(PREPEND CMAKE_PREFIX_PATH "${PIP_PROTOBUF_ROOT}")
endif()
# proto3 optional requires 3.15 or later. Pin the installed compiler and runtime together.
set(PIP_PROTOBUF_VERSION "3.20.3")
find_package(Protobuf ${PIP_PROTOBUF_VERSION}.0 EXACT CONFIG REQUIRED)
get_target_property(PIP_PROTOC protobuf::protoc IMPORTED_LOCATION)
if (NOT PIP_PROTOC)
    get_target_property(PIP_PROTOC_CONFIGS protobuf::protoc IMPORTED_CONFIGURATIONS)
    list(GET PIP_PROTOC_CONFIGS 0 PIP_PROTOC_CONFIG)
    get_target_property(PIP_PROTOC protobuf::protoc "IMPORTED_LOCATION_${PIP_PROTOC_CONFIG}")
endif()
execute_process(
    COMMAND "${PIP_PROTOC}" --version
    RESULT_VARIABLE PIP_PROTOC_RESULT
    OUTPUT_VARIABLE PIP_PROTOC_VERSION
    ERROR_VARIABLE PIP_PROTOC_ERROR
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
if (NOT PIP_PROTOC_RESULT STREQUAL "0" OR
    NOT PIP_PROTOC_VERSION STREQUAL "libprotoc ${PIP_PROTOBUF_VERSION}")
    message(FATAL_ERROR "Expected libprotoc ${PIP_PROTOBUF_VERSION} from Protobuf_DIR; "
        "got '${PIP_PROTOC_VERSION}' (${PIP_PROTOC_RESULT}): ${PIP_PROTOC_ERROR}")
endif()

qt_standard_project_setup(REQUIRES 6.12)
qt_policy(SET QTP0004 NEW)

qt_add_executable(PIPClient
    src/main.cpp
    src/diagnostics/runtime_diagnostics.cpp src/diagnostics/runtime_diagnostics.h
)
include(cmake/app_metadata.cmake)
pip_configure_app_metadata(PIPClient)

# The application and offline tests consume the same generated C++ definitions.
add_library(pip_protocol STATIC
    proto/client_to_server.proto
    proto/server_to_client.proto
)

# Linux 可执行文件没有 .exe 后缀；模块目录单独放置，避免与 PIPClient 产物同名。
qt_add_qml_module(PIPClient
    URI PIPClient
    VERSION 1.0
    OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/qml/PIPClient"
    IMPORT_PATH "${CMAKE_CURRENT_BINARY_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/qml"
    QML_FILES
        ui/Main.qml
        ui/components/BasicButton.qml
        ui/welcome/SceneBackdrop.qml
        ui/welcome/WelcomePage.qml
        ui/settings/AppPanel.qml
        ui/settings/VerticalSyncSettings.qml
        ui/settings/VideoSettings.qml
        ui/settings/ClientMenu.qml
        ui/settings/SessionSettings.qml
        ui/settings/SerialSettings.qml
        ui/settings/InputSettings.qml
        ui/settings/InputKeyboard.qml
        ui/settings/CheckResult.qml
        ui/settings/SettingsPage.qml
        ui/settings/PreparationOverview.qml
        ui/components/VideoButton.qml
        ui/components/VideoField.qml
        ui/components/VideoChoice.qml
        ui/video/VideoDebug.qml
        ui/video/VideoPlaceholder.qml
        ui/rmul/RmulHud.qml
)

# 所有资源使用固定 qrc 路径，两平台离线启动，不读取开发机绝对路径。
set(PIP_WELCOME_RESOURCES
    resources/welcome/arena.jpg
    resources/welcome/poster.jpg
    resources/fonts/barlow_bold.ttf
    resources/fonts/barlow_medium.ttf
    resources/fonts/ofl_barlow.txt
    resources/fonts/noto_sans_sc.ttf
    resources/fonts/ofl_noto_sans_sc.txt
    resources/welcome/notice.txt
    resources/icons/settings-2.svg
    resources/icons/activity.svg
    resources/icons/help.svg
    resources/icons/info.svg
    resources/icons/license_lucide.txt
    resources/icons/close_panel.svg
    resources/icons/menu_session.svg
    resources/icons/preparation_brand.svg
    resources/icons/video_off.svg
    resources/icons/chevron_down.svg
    resources/icons/menu_video.svg
    resources/icons/menu_display.svg
    resources/icons/menu_diagnostics.svg
    resources/icons/module_lamps/statusbar_signallamp_red_single.png
)
# 正式兵种素材与UI测试共用同一qrc路径；只打包大图，不引入设计HTML或地图资源。
foreach(pip_hud_team red blue)
    foreach(pip_hud_role heavy infantry_3 infantry_4 aerial sentry dart radar)
        list(APPEND PIP_WELCOME_RESOURCES
            resources/icons/robomaster_2027/standard/${pip_hud_team}_${pip_hud_role}.svg)
    endforeach()
endforeach()
qt_add_resources(PIPClient welcome_assets PREFIX "/" FILES ${PIP_WELCOME_RESOURCES})

set_target_properties(PIPClient PROPERTIES
    WIN32_EXECUTABLE TRUE
)

file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated")
protobuf_generate(
    TARGET pip_protocol
    LANGUAGE cpp
    PROTOC_OUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated"
    IMPORT_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/proto"
    APPEND_PATH
)

target_include_directories(pip_protocol PUBLIC
    "${CMAKE_CURRENT_BINARY_DIR}/generated"
)

target_link_libraries(pip_protocol PUBLIC protobuf::libprotobuf)
add_library(pip_input_state STATIC src/input/input_state.cpp src/input/input_state.h)
target_include_directories(pip_input_state PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(pip_input_state PUBLIC Qt6::Core)
add_library(pip_data_pipeline STATIC src/communication/data_pipeline.cpp src/communication/data_pipeline.h
    src/communication/protocol_catalog.cpp src/communication/protocol_catalog.h
    src/platform/background_timer_resolution.cpp src/platform/background_timer_resolution.h
    src/platform/deadline_timer.cpp src/platform/deadline_timer.h
    src/communication/control_cadence.h)
target_include_directories(pip_data_pipeline PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(pip_data_pipeline PUBLIC Qt6::Core Qt6::Network pip_protocol pip_input_state PRIVATE Qt6::Mqtt)
add_library(pip_app STATIC src/app/app_controller.cpp src/app/app_controller.h
    src/app/ui_fonts.cpp src/app/ui_fonts.h)
target_include_directories(pip_app PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(pip_app PUBLIC Qt6::Gui)
add_library(pip_diagnostics STATIC src/diagnostics/session_log.cpp src/diagnostics/session_log.h
    src/platform/crash_reporter.cpp src/platform/crash_reporter.h)
target_include_directories(pip_diagnostics PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(pip_diagnostics PUBLIC Qt6::Core)
qt_add_qml_module(pip_video
    STATIC
    URI PIPVideo
    VERSION 1.0
    DEPENDENCIES QtQuick
    NO_PLUGIN
    OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/PIPVideo"
    SOURCES
    src/video/video_assembler.cpp src/video/video_assembler.h
    src/video/hevc_decoder.cpp src/video/hevc_decoder.h
    src/video/video_frame.cpp
    src/video/video_frame_mailbox.h
    src/video/presentation_window.cpp src/video/presentation_window.h
    src/video/video_texture_node.cpp src/video/video_texture_node.h
    src/video/video_native_texture.cpp src/video/video_native_texture.h
    src/video/ffmpeg_log_bridge.cpp src/video/ffmpeg_log_bridge.h
    src/video/video_color_converter.cpp src/video/video_color_converter.h
    src/video/video_decode_devices.cpp src/video/video_decode_devices.h
    src/video/video_statistics.cpp src/video/video_statistics.h
    src/video/video_decode_queue.cpp src/video/video_decode_queue.h
    src/video/video_receiver.cpp src/video/video_receiver.h
    src/video/video_view.cpp src/video/video_view.h)
target_include_directories(pip_video PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
if (WIN32)
    # 解码适配层枚举真实 GPU，业务层不依赖 Windows 设备句柄。
    target_link_libraries(pip_video PRIVATE dxgi)
else()
    # EGL 入口运行时探测；仅需 Khronos 开发头文件，不直接链接 libva 或固定驱动。
    find_path(PIP_EGL_INCLUDE_DIR EGL/egl.h REQUIRED)
    target_include_directories(pip_video PRIVATE "${PIP_EGL_INCLUDE_DIR}")
endif()
target_include_directories(pip_video PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/video")
target_link_libraries(pip_video PUBLIC Qt6::Quick Qt6::Network PRIVATE pip_ffmpeg Qt6::GuiPrivate)
qt_add_shaders(pip_video video_shaders
    BATCHABLE
    PREFIX "/pip/video"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/src/video"
    FILES src/video/shaders/video.vert src/video/shaders/video.frag
    src/video/shaders/present.vert src/video/shaders/present.frag)
add_library(pip_serial STATIC
    src/communication/serial_frame_parser.cpp src/communication/serial_frame_parser.h
    src/communication/serial_controller.cpp src/communication/serial_controller.h)
target_include_directories(pip_serial PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(pip_serial PUBLIC pip_data_pipeline PRIVATE Qt6::SerialPort)
# 原生输入依赖只在适配 target 中；Wayland 协议固定 1.31 XML，构建时离线生成。
add_library(pip_input STATIC src/input/input_controller.cpp src/input/input_controller.h
    src/platform/native_input.h)
target_include_directories(pip_input PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(pip_input PUBLIC pip_data_pipeline Qt6::Gui PRIVATE Qt6::GuiPrivate)
if (WIN32)
    target_sources(pip_input PRIVATE src/platform/native_input_windows.cpp)
    target_link_libraries(pip_input PRIVATE user32)
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(PIP_INPUT_NATIVE REQUIRED IMPORTED_TARGET x11 xi wayland-client)
    find_program(PIP_WAYLAND_SCANNER wayland-scanner REQUIRED)
    foreach(PIP_INPUT_PROTOCOL relative-pointer pointer-constraints)
        set(PIP_INPUT_XML "${CMAKE_CURRENT_SOURCE_DIR}/resources/protocols/${PIP_INPUT_PROTOCOL}-unstable-v1.xml")
        set(PIP_INPUT_HEADER "${CMAKE_CURRENT_BINARY_DIR}/generated/${PIP_INPUT_PROTOCOL}-client.h")
        set(PIP_INPUT_CODE "${CMAKE_CURRENT_BINARY_DIR}/generated/${PIP_INPUT_PROTOCOL}-protocol.c")
        add_custom_command(OUTPUT "${PIP_INPUT_HEADER}" "${PIP_INPUT_CODE}"
            COMMAND "${PIP_WAYLAND_SCANNER}" client-header "${PIP_INPUT_XML}" "${PIP_INPUT_HEADER}"
            COMMAND "${PIP_WAYLAND_SCANNER}" private-code "${PIP_INPUT_XML}" "${PIP_INPUT_CODE}"
            DEPENDS "${PIP_INPUT_XML}" VERBATIM)
        target_sources(pip_input PRIVATE "${PIP_INPUT_HEADER}" "${PIP_INPUT_CODE}")
    endforeach()
    target_sources(pip_input PRIVATE src/platform/native_input_linux.cpp)
    target_include_directories(pip_input PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
    target_link_libraries(pip_input PRIVATE PkgConfig::PIP_INPUT_NATIVE)
endif()
add_library(pip_session STATIC src/app/match_session.cpp src/app/match_session.h)
target_include_directories(pip_session PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(pip_session PUBLIC pip_data_pipeline pip_video pip_serial pip_input)
target_link_libraries(PIPClient PRIVATE Qt6::Quick Qt6::QuickControls2 Qt6::Svg pip_data_pipeline pip_app pip_video pip_session pip_diagnostics)
target_compile_definitions(PIPClient PRIVATE PIP_VERSION="${PIP_VERSION_FULL}")
# 与二进制一同记录构建身份，未知源码归档明确标为 unknown；不记录开发机绝对路径。
find_package(Git QUIET)
set(PIP_BUILD_REVISION "unknown")
if (GIT_FOUND)
    execute_process(COMMAND "${GIT_EXECUTABLE}" describe --always --dirty
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        OUTPUT_VARIABLE PIP_GIT_REVISION OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET RESULT_VARIABLE PIP_GIT_RESULT)
    if (PIP_GIT_RESULT EQUAL 0 AND PIP_GIT_REVISION MATCHES "^[A-Za-z0-9._-]+$")
        set(PIP_BUILD_REVISION "${PIP_GIT_REVISION}")
    endif()
endif()
target_compile_definitions(PIPClient PRIVATE
    PIP_BUILD_REVISION="${PIP_BUILD_REVISION}" PIP_BUILD_TYPE="$<CONFIG>")
if (WIN32)
    # 独立安装的 Qt 模块不会进入 Qt kit 的 bin；构建目录也须可由 IDE 直接启动，
    # 不能只在安装阶段部署，或依赖终端临时补充的 PATH。
    add_custom_command(TARGET PIPClient POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${PIP_FFMPEG_RUNTIME_FILES}
            "$<TARGET_FILE:Qt6::Mqtt>" "$<TARGET_FILE:Qt6::SerialPort>"
            "$<TARGET_FILE_DIR:PIPClient>"
        COMMENT "Deploying FFmpeg and external Qt module runtime")
    install(FILES ${PIP_FFMPEG_RUNTIME_FILES} DESTINATION bin)
endif()

include(CTest)
if (BUILD_TESTING)
    add_subdirectory(tests)
endif()

include(GNUInstallDirs)
install(TARGETS PIPClient
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
# 生成的 Wayland 客户端包含这些上游许可，随安装保留版权原文。
install(FILES resources/protocols/relative-pointer-unstable-v1.xml
    resources/protocols/pointer-constraints-unstable-v1.xml
    DESTINATION "${CMAKE_INSTALL_DATADIR}/PIPClient/licenses/wayland-protocols")
install(FILES resources/licenses/ffmpeg_lgpl_2_1.txt resources/licenses/ffmpeg_license.md
    DESTINATION "${CMAKE_INSTALL_DATADIR}/PIPClient/licenses")

# 使用所选 Qt kit 自带部署规则收集 QML 插件和运行库；安装前缀由调用者指定。
include(cmake/app_deployment.cmake)
pip_configure_app_deployment(PIPClient)
include(cmake/packaging.cmake)
