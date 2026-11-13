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

# All directly linked components must describe FFmpeg 7's C ABI. SDK directory names
# alone are insufficient when headers and shared runtime are supplied separately.
foreach(pip_component_version "avutil/version.h|LIBAVUTIL_VERSION_MAJOR|59"
        "swscale/version_major.h|LIBSWSCALE_VERSION_MAJOR|8")
    string(REPLACE "|" ";" pip_version_fields "${pip_component_version}")
    list(GET pip_version_fields 0 pip_version_header)
    list(GET pip_version_fields 1 pip_version_macro)
    list(GET pip_version_fields 2 pip_version_major)
    file(READ "${PIP_FFMPEG_INCLUDE_DIR}/lib${pip_version_header}" pip_version_contents)
    if (NOT pip_version_contents MATCHES "${pip_version_macro}[ \t]+${pip_version_major}[\r\n]")
        message(FATAL_ERROR "PIP video requires FFmpeg 7.x (${pip_version_macro}=${pip_version_major})")
    endif()
endforeach()

# A complete SDK owns its runtime. Header/import-only SDKs can explicitly name a separate
# directory; the legacy Qt-kit fallback remains visible and never consults PATH.
if (WIN32)
    set(PIP_FFMPEG_RUNTIME_DIR "" CACHE PATH "Directory containing the complete FFmpeg 7 DLL closure")
    if (PIP_FFMPEG_RUNTIME_DIR)
        set(pip_runtime_directory "${PIP_FFMPEG_RUNTIME_DIR}")
    elseif (IS_DIRECTORY "${PIP_FFMPEG_ROOT}/bin")
        set(pip_runtime_directory "${PIP_FFMPEG_ROOT}/bin")
    else()
        get_target_property(PIP_QMAKE Qt6::qmake IMPORTED_LOCATION)
        get_filename_component(pip_runtime_directory "${PIP_QMAKE}" DIRECTORY)
        message(WARNING "FFmpeg SDK has no bin directory; using selected Qt kit runtime: "
            "${pip_runtime_directory}. Set PIP_FFMPEG_RUNTIME_DIR to make this selection explicit.")
    endif()
    include("${CMAKE_CURRENT_LIST_DIR}/ffmpeg_runtime.cmake")
    pip_collect_ffmpeg_runtime("${pip_runtime_directory}" PIP_FFMPEG_RUNTIME_FILES)
    message(STATUS "FFmpeg runtime closure: ${PIP_FFMPEG_RUNTIME_FILES}")
endif()
