# FFmpeg 7.x C ABI only; no runtime fetching or host-specific paths in the build.
# Linux distribution SDKs and Windows shared SDKs use the same target contract.
set(PIP_FFMPEG_ROOT "" CACHE PATH "FFmpeg 7 shared development prefix")
find_path(PIP_FFMPEG_INCLUDE_DIR libavcodec/avcodec.h
    HINTS "${PIP_FFMPEG_ROOT}/include" REQUIRED)
file(READ "${PIP_FFMPEG_INCLUDE_DIR}/libavcodec/version_major.h" PIP_AVCODEC_VERSION)
if (NOT PIP_AVCODEC_VERSION MATCHES "LIBAVCODEC_VERSION_MAJOR[ \t]+61[\r\n]")
    message(FATAL_ERROR "PIP video requires FFmpeg 7.x (libavcodec 61)")
endif()
add_library(pip_ffmpeg INTERFACE)
target_include_directories(pip_ffmpeg INTERFACE "${PIP_FFMPEG_INCLUDE_DIR}")
foreach(PIP_FFMPEG_COMPONENT avcodec avutil swscale)
    find_library(PIP_${PIP_FFMPEG_COMPONENT}_LIBRARY
        NAMES ${PIP_FFMPEG_COMPONENT}
        HINTS "${PIP_FFMPEG_ROOT}/lib" REQUIRED)
    target_link_libraries(pip_ffmpeg INTERFACE "${PIP_${PIP_FFMPEG_COMPONENT}_LIBRARY}")
endforeach()

# MinGW/MSVC 开发运行同样需要 DLL；优先 SDK，其次匹配 Qt kit 自带的 FFmpeg 7。
if (WIN32)
    get_target_property(PIP_QMAKE Qt6::qmake IMPORTED_LOCATION)
    get_filename_component(PIP_QT_BIN "${PIP_QMAKE}" DIRECTORY)
    set(PIP_FFMPEG_RUNTIME_FILES "")
    foreach(PIP_FFMPEG_DLL avcodec-61.dll avutil-59.dll swscale-8.dll)
        string(MAKE_C_IDENTIFIER "${PIP_FFMPEG_DLL}" PIP_DLL_VARIABLE)
        find_file(PIP_${PIP_DLL_VARIABLE}_PATH "${PIP_FFMPEG_DLL}"
            HINTS "${PIP_FFMPEG_ROOT}/bin" "${PIP_QT_BIN}" REQUIRED)
        list(APPEND PIP_FFMPEG_RUNTIME_FILES "${PIP_${PIP_DLL_VARIABLE}_PATH}")
    endforeach()
endif()
