# Tests are organized by the product module they exercise; target names remain stable.
find_package(Qt6 ${Qt6_VERSION} EXACT REQUIRED COMPONENTS Test)
add_executable(background_timer_resolution_test platform/background_timer_resolution_test.cpp)
target_link_libraries(background_timer_resolution_test PRIVATE pip_data_pipeline Qt6::Test)
add_test(NAME background_timer_resolution COMMAND background_timer_resolution_test -o -,txt)
# 手动硬件工具不注册为 CTest；只有显式指定设备/端点才访问真实串口。
add_executable(serial_pipeline_probe "${PROJECT_SOURCE_DIR}/tools/serial_pipeline_probe.cpp")
target_link_libraries(serial_pipeline_probe PRIVATE pip_serial)
add_executable(serial_frame_parser_test communication/serial_frame_parser_test.cpp)
target_include_directories(serial_frame_parser_test PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(serial_frame_parser_test PRIVATE pip_serial Qt6::Test)
add_test(NAME serial_frame_parser COMMAND serial_frame_parser_test
    -o -,txt -o serial_frame_parser_test_results.txt,txt)
add_executable(serial_controller_test communication/serial_controller_test.cpp)
target_include_directories(serial_controller_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(serial_controller_test PRIVATE pip_serial Qt6::Test Qt6::SerialPort)
add_test(NAME serial_controller COMMAND serial_controller_test
    -o -,txt -o serial_controller_test_results.txt,txt)
set_tests_properties(serial_controller PROPERTIES TIMEOUT 30)
if (WIN32)
    # 扫描真实 SDK 闭包，并以隔离夹具验证缺失传递依赖、错误 ABI 和 PE 架构拒绝。
    list(GET PIP_FFMPEG_RUNTIME_FILES 0 PIP_FFMPEG_RUNTIME_SAMPLE)
    get_filename_component(PIP_FFMPEG_RUNTIME_TEST_DIR "${PIP_FFMPEG_RUNTIME_SAMPLE}" DIRECTORY)
    add_test(NAME ffmpeg_runtime
        COMMAND "${CMAKE_COMMAND}"
            "-DSOURCE_ROOT=${PROJECT_SOURCE_DIR}"
            "-DOUTPUT_ROOT=${CMAKE_CURRENT_BINARY_DIR}/ffmpeg_runtime_test"
            "-DRUNTIME_DIR=${PIP_FFMPEG_RUNTIME_TEST_DIR}"
            "-DOBJDUMP=${PIP_FFMPEG_RUNTIME_IMPORT_TOOL}"
            "-DTARGET_POINTER_SIZE=${CMAKE_SIZEOF_VOID_P}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/ffmpeg_runtime_test.cmake")
    set_tests_properties(ffmpeg_runtime PROPERTIES TIMEOUT 60)
endif()
add_executable(session_log_test diagnostics/session_log_test.cpp)
target_link_libraries(session_log_test PRIVATE pip_diagnostics Qt6::Test)
add_test(NAME session_log COMMAND session_log_test -o -,txt)
set_tests_properties(session_log PROPERTIES TIMEOUT 20)
add_executable(crash_reporter_helper diagnostics/crash_reporter_helper.cpp)
target_link_libraries(crash_reporter_helper PRIVATE pip_diagnostics)
add_executable(crash_reporter_test diagnostics/crash_reporter_test.cpp)
target_link_libraries(crash_reporter_test PRIVATE pip_diagnostics Qt6::Test)
target_compile_definitions(crash_reporter_test PRIVATE
    PIP_CRASH_HELPER_PATH="$<TARGET_FILE:crash_reporter_helper>")
add_dependencies(crash_reporter_test crash_reporter_helper)
add_test(NAME crash_reporter COMMAND crash_reporter_test -o -,txt)
set_tests_properties(crash_reporter PROPERTIES TIMEOUT 30)
add_executable(ffmpeg_log_bridge_test video/ffmpeg_log_bridge_test.cpp)
target_link_libraries(ffmpeg_log_bridge_test PRIVATE pip_video pip_ffmpeg Qt6::Test)
add_test(NAME ffmpeg_log_bridge COMMAND ffmpeg_log_bridge_test -o -,txt)
add_executable(video_decode_queue_test video/video_decode_queue_test.cpp
    "${PROJECT_SOURCE_DIR}/src/video/video_decode_queue.cpp" "${PROJECT_SOURCE_DIR}/src/video/video_decode_queue.h")
target_include_directories(video_decode_queue_test PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(video_decode_queue_test PRIVATE Qt6::Test)
add_test(NAME video_decode_queue COMMAND video_decode_queue_test -o -,txt)
set_tests_properties(video_decode_queue PROPERTIES TIMEOUT 10)
add_executable(video_statistics_test video/video_statistics_test.cpp
    "${PROJECT_SOURCE_DIR}/src/video/video_statistics.cpp" "${PROJECT_SOURCE_DIR}/src/video/video_statistics.h")
target_include_directories(video_statistics_test PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(video_statistics_test PRIVATE Qt6::Test)
add_test(NAME video_statistics COMMAND video_statistics_test -o -,txt)
add_executable(video_assembler_test video/video_assembler_test.cpp
    "${PROJECT_SOURCE_DIR}/src/video/video_assembler.cpp" "${PROJECT_SOURCE_DIR}/src/video/video_assembler.h")
target_include_directories(video_assembler_test PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(video_assembler_test PRIVATE Qt6::Test)
add_test(NAME video_assembler COMMAND video_assembler_test -o -,txt)
add_executable(hevc_decoder_test video/hevc_decoder_test.cpp)
add_executable(video_color_converter_test video/video_color_converter_test.cpp)
add_executable(video_texture_test video/video_texture_test.cpp)
target_link_libraries(video_texture_test PRIVATE pip_video pip_ffmpeg Qt6::Test)
target_compile_definitions(video_texture_test PRIVATE
    PIP_VIDEO_HD_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/video_hd.hevc"
    PIP_VIDEO_HD10_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/video_hd10.hevc")
add_test(NAME video_texture COMMAND video_texture_test
    -o -,txt -o video_texture_test_results.txt,txt)
set_tests_properties(video_texture PROPERTIES TIMEOUT 90)
target_link_libraries(video_color_converter_test PRIVATE pip_video pip_ffmpeg Qt6::Test)
add_test(NAME video_color_converter COMMAND video_color_converter_test -o -,txt)
target_link_libraries(hevc_decoder_test PRIVATE pip_video Qt6::Test)
target_compile_definitions(hevc_decoder_test PRIVATE
    PIP_VIDEO_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/video.hevc"
    PIP_VIDEO_HD_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/video_hd.hevc")
add_test(NAME hevc_decoder COMMAND hevc_decoder_test -o -,txt)
set_tests_properties(hevc_decoder PROPERTIES TIMEOUT 60)
add_executable(video_receiver_test video/video_receiver_test.cpp)
target_link_libraries(video_receiver_test PRIVATE pip_video Qt6::Test)
target_compile_definitions(video_receiver_test PRIVATE
    PIP_VIDEO_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/video.hevc"
    PIP_VIDEO_HD_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/video_hd.hevc")
# Windows GUI 子系统可能不向 CTest 管道写 QtTest 文本；同时落盘保留失败断言。
add_test(NAME video_receiver COMMAND video_receiver_test
    -o -,txt -o video_receiver_test_results.txt,txt)
# 真实硬件设备创建使 40 次重配置比 CPU 路径更慢；单次 stop 的 1 秒断言仍保留。
set_tests_properties(video_receiver PROPERTIES TIMEOUT 90)
add_executable(protocol_2027_test protocol/protocol_2027_test.cpp)
target_link_libraries(protocol_2027_test PRIVATE pip_protocol)
add_test(NAME protocol_2027
    COMMAND protocol_2027_test "${CMAKE_CURRENT_SOURCE_DIR}/protocol/protocol_2027_schema.txt")
add_executable(data_pipeline_test communication/data_pipeline_test.cpp)
target_link_libraries(data_pipeline_test PRIVATE pip_data_pipeline Qt6::Test Qt6::Network Qt6::Quick)
add_test(NAME data_pipeline COMMAND data_pipeline_test -o -,txt)
set_tests_properties(data_pipeline PROPERTIES TIMEOUT 40)
add_executable(match_session_test app/match_session_test.cpp)
target_link_libraries(match_session_test PRIVATE pip_session Qt6::Test)
target_compile_definitions(match_session_test PRIVATE
    PIP_VIDEO_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/video.hevc")
add_test(NAME match_session COMMAND match_session_test -o -,txt)
set_tests_properties(match_session PROPERTIES TIMEOUT 30)
add_executable(app_controller_test app/app_controller_test.cpp)
target_link_libraries(app_controller_test PRIVATE pip_app Qt6::Test)
add_test(NAME app_controller COMMAND app_controller_test -o -,txt)
add_executable(welcome_ui_test ui/welcome_ui_test.cpp)
add_executable(video_window_cover "${PROJECT_SOURCE_DIR}/tools/video_window_cover.cpp")
target_link_libraries(video_window_cover PRIVATE Qt6::Quick)

# 仅为 Windows 架构对照，不注册 CTest、不安装，也不成为产品的可选/默认后端。
if (WIN32)
    add_executable(video_native_present_probe EXCLUDE_FROM_ALL
        "${PROJECT_SOURCE_DIR}/tools/video_native_present_probe.cpp")
    target_include_directories(video_native_present_probe PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_compile_definitions(video_native_present_probe PRIVATE
        PIP_VIDEO_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/video.hevc"
        PIP_VIDEO_HD_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/video_hd.hevc")
    target_link_libraries(video_native_present_probe PRIVATE pip_video pip_ffmpeg d3d11 dxgi d3dcompiler)
endif()
target_sources(welcome_ui_test PRIVATE "${PROJECT_SOURCE_DIR}/src/diagnostics/runtime_diagnostics.cpp")
target_link_libraries(welcome_ui_test PRIVATE pip_app pip_data_pipeline pip_video Qt6::Test
    Qt6::Quick Qt6::QuickControls2 Qt6::Svg pip_session pip_diagnostics)
target_compile_definitions(welcome_ui_test PRIVATE
    PIP_MAIN_QML_PATH="${PROJECT_SOURCE_DIR}/ui/Main.qml"
    PIP_VIDEO_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/video.hevc"
    PIP_VIDEO_HD_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/video_hd.hevc"
    PIP_VERSION="${PIP_VERSION_FULL}")
# Keep runtime qrc URLs identical while the test target lives in its own build subtree.
set(pip_test_resources ${PIP_WELCOME_RESOURCES})
list(TRANSFORM pip_test_resources PREPEND "${PROJECT_SOURCE_DIR}/")
qt_add_resources(welcome_ui_test welcome_test_assets PREFIX "/"
    BASE "${PROJECT_SOURCE_DIR}" FILES ${pip_test_resources})
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
set_tests_properties(data_pipeline welcome_ui video_texture PROPERTIES
    ENVIRONMENT "${PIP_GUI_TEST_ENVIRONMENT}")

# Helpers are shared by communication, session, video and UI tests; production has no dependency
# on these headers. Explicit test-only roots keep includes independent of working directories.
foreach(pip_test_target data_pipeline_test match_session_test video_receiver_test welcome_ui_test)
    target_include_directories(${pip_test_target} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
endforeach()
