cmake_minimum_required(VERSION 3.25)

project(PIPClient VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 两平台固定同一 Qt 基线；拒绝旧 kit，防止 IDE 缓存悄悄选回已退役版本。
find_package(Qt6 6.12.0 EXACT REQUIRED COMPONENTS Quick QuickControls2 Svg Network)
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
        ui/components/VideoButton.qml
        ui/components/VideoField.qml
        ui/components/VideoChoice.qml
        ui/video/VideoDebug.qml
        ui/video/VideoPlaceholder.qml
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
    resources/icons/video_off.svg
    resources/icons/chevron_down.svg
    resources/icons/menu_video.svg
    resources/icons/menu_display.svg
    resources/icons/menu_diagnostics.svg
)
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
add_library(pip_data_pipeline STATIC src/communication/data_pipeline.cpp src/communication/data_pipeline.h
    src/communication/protocol_catalog.cpp src/communication/protocol_catalog.h)
target_include_directories(pip_data_pipeline PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(pip_data_pipeline PUBLIC Qt6::Core Qt6::Network pip_protocol PRIVATE Qt6::Mqtt)
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
endif()
target_include_directories(pip_video PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/video")
target_link_libraries(pip_video PUBLIC Qt6::Quick Qt6::Network PRIVATE pip_ffmpeg)
add_library(pip_session STATIC src/app/match_session.cpp src/app/match_session.h)
target_include_directories(pip_session PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(pip_session PUBLIC pip_data_pipeline pip_video)
target_link_libraries(PIPClient PRIVATE Qt6::Quick Qt6::QuickControls2 Qt6::Svg pip_data_pipeline pip_app pip_video pip_session pip_diagnostics)
target_compile_definitions(PIPClient PRIVATE PIP_VERSION="${PROJECT_VERSION}")
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
    add_custom_command(TARGET PIPClient POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${PIP_FFMPEG_RUNTIME_FILES}
            "$<TARGET_FILE_DIR:PIPClient>"
        COMMENT "Deploying FFmpeg shared runtime")
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
install(FILES resources/licenses/ffmpeg_lgpl_2_1.txt resources/licenses/ffmpeg_license.md
    DESTINATION "${CMAKE_INSTALL_DATADIR}/PIPClient/licenses")

# 使用所选 Qt kit 自带部署规则收集 QML 插件和运行库；安装前缀由调用者指定。
include(cmake/app_deployment.cmake)
pip_configure_app_deployment(PIPClient)
set(CPACK_PACKAGE_NAME "PIPClient")
set(CPACK_PACKAGE_VENDOR "PIP")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "RoboMaster video and referee-system client")
if (WIN32)
    set(CPACK_GENERATOR "ZIP")
else()
    set(CPACK_GENERATOR "TGZ")
endif()
include(CPack)
