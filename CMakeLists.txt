cmake_minimum_required(VERSION 3.24)

project(PIPClient VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 6.5 REQUIRED COMPONENTS Quick QuickControls2 Svg Network)
include(cmake/ffmpeg.cmake)

# Use the Qt MQTT module matching the selected Qt kit; no network fetch during app builds.
set(PIP_MQTT_ROOT "" CACHE PATH "Qt MQTT installation prefix")
if (PIP_MQTT_ROOT)
    list(PREPEND CMAKE_PREFIX_PATH "${PIP_MQTT_ROOT}")
endif()
find_package(Qt6Mqtt ${Qt6_VERSION} EXACT REQUIRED
    HINTS "${PIP_MQTT_ROOT}/lib/cmake/Qt6Mqtt")

# Prefer the repository-local MinGW build so protoc and libprotobuf use the
# same compiler ABI as the Qt kit. A caller may override this for CI or a
# separately provisioned toolchain.
set(PIP_PROTOBUF_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/protobuf-mingw"
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

qt_standard_project_setup(REQUIRES 6.5)
qt_policy(SET QTP0004 NEW)

qt_add_executable(PIPClient
    src/main.cpp
    src/app/runtime_diagnostics.cpp src/app/runtime_diagnostics.h
)
include(cmake/app_metadata.cmake)
pip_configure_app_metadata(PIPClient)

# The application and offline tests consume the same generated C++ definitions.
add_library(pip_protocol STATIC
    proto/client_to_server.proto
    proto/server_to_client.proto
)

qt_add_qml_module(PIPClient
    URI PIPClient
    VERSION 1.0
    IMPORT_PATH "${CMAKE_CURRENT_BINARY_DIR}"
    QML_FILES
        ui/Main.qml
        ui/BasicButton.qml
        ui/SceneBackdrop.qml
        ui/WelcomePage.qml
        ui/AppPanel.qml
        ui/VerticalSyncSettings.qml
        ui/VideoSettings.qml
        ui/ClientMenu.qml
        ui/SessionSettings.qml
        ui/VideoButton.qml
        ui/VideoField.qml
        ui/VideoChoice.qml
        ui/VideoDebug.qml
        ui/VideoPlaceholder.qml
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
add_library(pip_data_pipeline STATIC src/core/data_pipeline.cpp src/core/data_pipeline.h
    src/core/protocol_catalog.cpp src/core/protocol_catalog.h)
target_include_directories(pip_data_pipeline PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(pip_data_pipeline PUBLIC Qt6::Core Qt6::Network pip_protocol PRIVATE Qt6::Mqtt)
add_library(pip_app STATIC src/app/app_controller.cpp src/app/app_controller.h
    src/app/ui_fonts.cpp src/app/ui_fonts.h)
target_include_directories(pip_app PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(pip_app PUBLIC Qt6::Gui)
add_library(pip_diagnostics STATIC src/app/session_log.cpp src/app/session_log.h
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
    find_package(Qt6 ${Qt6_VERSION} EXACT REQUIRED COMPONENTS Test)
    if (WIN32)
        # 扫描真实 SDK 闭包，并以隔离夹具验证缺失传递依赖、错误 ABI 和 PE 架构拒绝。
        list(GET PIP_FFMPEG_RUNTIME_FILES 0 PIP_FFMPEG_RUNTIME_SAMPLE)
        get_filename_component(PIP_FFMPEG_RUNTIME_TEST_DIR "${PIP_FFMPEG_RUNTIME_SAMPLE}" DIRECTORY)
        add_test(NAME ffmpeg_runtime
            COMMAND "${CMAKE_COMMAND}"
                "-DSOURCE_ROOT=${CMAKE_CURRENT_SOURCE_DIR}"
                "-DOUTPUT_ROOT=${CMAKE_CURRENT_BINARY_DIR}/ffmpeg_runtime_test"
                "-DRUNTIME_DIR=${PIP_FFMPEG_RUNTIME_TEST_DIR}"
                "-DOBJDUMP=${PIP_FFMPEG_RUNTIME_IMPORT_TOOL}"
                "-DTARGET_POINTER_SIZE=${CMAKE_SIZEOF_VOID_P}"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/ffmpeg_runtime_test.cmake")
        set_tests_properties(ffmpeg_runtime PROPERTIES TIMEOUT 60)
    endif()
    add_executable(session_log_test tests/session_log_test.cpp)
    target_link_libraries(session_log_test PRIVATE pip_diagnostics Qt6::Test)
    add_test(NAME session_log COMMAND session_log_test -o -,txt)
    set_tests_properties(session_log PROPERTIES TIMEOUT 20)
    add_executable(crash_reporter_helper tests/crash_reporter_helper.cpp)
    target_link_libraries(crash_reporter_helper PRIVATE pip_diagnostics)
    add_executable(crash_reporter_test tests/crash_reporter_test.cpp)
    target_link_libraries(crash_reporter_test PRIVATE pip_diagnostics Qt6::Test)
    target_compile_definitions(crash_reporter_test PRIVATE
        PIP_CRASH_HELPER_PATH="$<TARGET_FILE:crash_reporter_helper>")
    add_dependencies(crash_reporter_test crash_reporter_helper)
    add_test(NAME crash_reporter COMMAND crash_reporter_test -o -,txt)
    set_tests_properties(crash_reporter PROPERTIES TIMEOUT 30)
    add_executable(ffmpeg_log_bridge_test tests/ffmpeg_log_bridge_test.cpp)
    target_link_libraries(ffmpeg_log_bridge_test PRIVATE pip_video pip_ffmpeg Qt6::Test)
    add_test(NAME ffmpeg_log_bridge COMMAND ffmpeg_log_bridge_test -o -,txt)
    add_executable(video_decode_queue_test tests/video_decode_queue_test.cpp
        src/video/video_decode_queue.cpp src/video/video_decode_queue.h)
    target_include_directories(video_decode_queue_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(video_decode_queue_test PRIVATE Qt6::Test)
    add_test(NAME video_decode_queue COMMAND video_decode_queue_test -o -,txt)
    set_tests_properties(video_decode_queue PROPERTIES TIMEOUT 10)
    add_executable(video_statistics_test tests/video_statistics_test.cpp
        src/video/video_statistics.cpp src/video/video_statistics.h)
    target_include_directories(video_statistics_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(video_statistics_test PRIVATE Qt6::Test)
    add_test(NAME video_statistics COMMAND video_statistics_test -o -,txt)
    add_executable(video_assembler_test tests/video_assembler_test.cpp
        src/video/video_assembler.cpp src/video/video_assembler.h)
    target_include_directories(video_assembler_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(video_assembler_test PRIVATE Qt6::Test)
    add_test(NAME video_assembler COMMAND video_assembler_test -o -,txt)
    add_executable(hevc_decoder_test tests/hevc_decoder_test.cpp)
    add_executable(video_color_converter_test tests/video_color_converter_test.cpp)
    target_link_libraries(video_color_converter_test PRIVATE pip_video pip_ffmpeg Qt6::Test)
    add_test(NAME video_color_converter COMMAND video_color_converter_test -o -,txt)
    target_link_libraries(hevc_decoder_test PRIVATE pip_video Qt6::Test)
    target_compile_definitions(hevc_decoder_test PRIVATE
        PIP_VIDEO_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/video.hevc"
        PIP_VIDEO_HD_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/video_hd.hevc")
    add_test(NAME hevc_decoder COMMAND hevc_decoder_test -o -,txt)
    set_tests_properties(hevc_decoder PROPERTIES TIMEOUT 60)
    add_executable(video_receiver_test tests/video_receiver_test.cpp)
    target_link_libraries(video_receiver_test PRIVATE pip_video Qt6::Test)
    target_compile_definitions(video_receiver_test PRIVATE
        PIP_VIDEO_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/video.hevc"
        PIP_VIDEO_HD_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/video_hd.hevc")
    # Windows GUI 子系统可能不向 CTest 管道写 QtTest 文本；同时落盘保留失败断言。
    add_test(NAME video_receiver COMMAND video_receiver_test
        -o -,txt -o video_receiver_test_results.txt,txt)
    # 真实硬件设备创建使 40 次重配置比 CPU 路径更慢；单次 stop 的 1 秒断言仍保留。
    set_tests_properties(video_receiver PROPERTIES TIMEOUT 90)
    add_executable(protocol_2027_test tests/protocol_2027_test.cpp)
    target_link_libraries(protocol_2027_test PRIVATE pip_protocol)
    add_test(NAME protocol_2027
        COMMAND protocol_2027_test "${CMAKE_CURRENT_SOURCE_DIR}/tests/protocol_2027_schema.txt")
    add_executable(data_pipeline_test tests/data_pipeline_test.cpp)
    target_link_libraries(data_pipeline_test PRIVATE pip_data_pipeline Qt6::Test Qt6::Network Qt6::Quick)
    add_test(NAME data_pipeline COMMAND data_pipeline_test -o -,txt)
    set_tests_properties(data_pipeline PROPERTIES TIMEOUT 40)
    add_executable(match_session_test tests/match_session_test.cpp)
    target_link_libraries(match_session_test PRIVATE pip_session Qt6::Test)
    target_compile_definitions(match_session_test PRIVATE
        PIP_VIDEO_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/video.hevc")
    add_test(NAME match_session COMMAND match_session_test -o -,txt)
    set_tests_properties(match_session PROPERTIES TIMEOUT 30)
    add_executable(app_controller_test tests/app_controller_test.cpp)
    target_link_libraries(app_controller_test PRIVATE pip_app Qt6::Test)
    add_test(NAME app_controller COMMAND app_controller_test -o -,txt)
    add_executable(welcome_ui_test tests/welcome_ui_test.cpp)
    target_link_libraries(welcome_ui_test PRIVATE pip_app pip_data_pipeline pip_video Qt6::Test
        Qt6::Quick Qt6::QuickControls2 Qt6::Svg pip_session)
    target_compile_definitions(welcome_ui_test PRIVATE
        PIP_MAIN_QML_PATH="${CMAKE_CURRENT_SOURCE_DIR}/ui/Main.qml"
        PIP_VIDEO_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/video.hevc"
        PIP_VIDEO_HD_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/video_hd.hevc"
        PIP_VERSION="${PROJECT_VERSION}")
    qt_add_resources(welcome_ui_test welcome_test_assets PREFIX "/" FILES ${PIP_WELCOME_RESOURCES})
    add_test(NAME welcome_ui COMMAND welcome_ui_test -o -,txt)
    # 构建目录可能残留 windeployqt 的 windows-only 插件。按所选 kit 的导入目标定位
    # offscreen 插件，避免测试使用陈旧部署目录而弹出平台初始化失败对话框。
    set(PIP_GUI_TEST_ENVIRONMENT
        "QT_QPA_PLATFORM=offscreen"
        "QT_QPA_PLATFORM_PLUGIN_PATH=$<TARGET_FILE_DIR:Qt6::QOffscreenIntegrationPlugin>"
        "QT_QUICK_CONTROLS_STYLE=Basic")
    if (WIN32)
        # offscreen 不使用 Windows 字体枚举；从系统目录加载中文回退字体。
        list(APPEND PIP_GUI_TEST_ENVIRONMENT "QT_QPA_FONTDIR=$ENV{WINDIR}/Fonts")
    endif()
    set_tests_properties(data_pipeline welcome_ui PROPERTIES
        ENVIRONMENT "${PIP_GUI_TEST_ENVIRONMENT}")
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
