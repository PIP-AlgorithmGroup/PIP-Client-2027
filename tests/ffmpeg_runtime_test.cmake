# Run with the product's actual Windows FFmpeg directory and compiler PE inspector.
# Every fixture is written under OUTPUT_ROOT; no source SDK or system kit is modified.
cmake_minimum_required(VERSION 3.22)
foreach(pip_required SOURCE_ROOT OUTPUT_ROOT RUNTIME_DIR OBJDUMP TARGET_POINTER_SIZE)
    if (NOT DEFINED ${pip_required} OR "${${pip_required}}" STREQUAL "")
        message(FATAL_ERROR "Missing test argument: ${pip_required}")
    endif()
endforeach()
set(CMAKE_OBJDUMP "${OBJDUMP}")
set(CMAKE_SIZEOF_VOID_P "${TARGET_POINTER_SIZE}")
include("${SOURCE_ROOT}/cmake/ffmpeg_runtime.cmake")

if (DEFINED SCAN_DIRECTORY)
    pip_collect_ffmpeg_runtime("${SCAN_DIRECTORY}" pip_closure)
    file(WRITE "${OUTPUT_ROOT}/closure.txt" "${pip_closure}")
    return()
endif()

pip_collect_ffmpeg_runtime("${RUNTIME_DIR}" pip_reference)
get_filename_component(pip_runtime_directory "${RUNTIME_DIR}" REALPATH)
file(MAKE_DIRECTORY "${OUTPUT_ROOT}")
string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef pip_suffix)
set(pip_run "${OUTPUT_ROOT}/run_${pip_suffix}")
file(MAKE_DIRECTORY "${pip_run}")

# A child configure inspects exactly the same module and selected tool as the product.
function(pip_expect_failure directory expected)
    set(pip_pointer_size "${TARGET_POINTER_SIZE}")
    if (ARGC GREATER 2)
        set(pip_pointer_size "${ARGV2}")
    endif()
    execute_process(COMMAND "${CMAKE_COMMAND}"
        "-DSOURCE_ROOT=${SOURCE_ROOT}" "-DOUTPUT_ROOT=${OUTPUT_ROOT}"
        "-DRUNTIME_DIR=${RUNTIME_DIR}" "-DOBJDUMP=${OBJDUMP}"
        "-DTARGET_POINTER_SIZE=${pip_pointer_size}"
        "-DSCAN_DIRECTORY=${directory}" -P "${CMAKE_CURRENT_LIST_FILE}"
        RESULT_VARIABLE pip_result OUTPUT_VARIABLE pip_out ERROR_VARIABLE pip_error TIMEOUT 45)
    if (pip_result STREQUAL "0" OR NOT "${pip_out}${pip_error}" MATCHES "${expected}")
        message(FATAL_ERROR "Unexpected runtime failure result (${expected}):\n${pip_out}${pip_error}")
    endif()
endfunction()

foreach(pip_missing swresample-5.dll libwinpthread-1.dll)
    set(pip_fixture "${pip_run}/missing_${pip_missing}")
    file(MAKE_DIRECTORY "${pip_fixture}")
    set(pip_was_present FALSE)
    foreach(pip_source IN LISTS pip_reference)
        get_filename_component(pip_name "${pip_source}" NAME)
        string(TOLOWER "${pip_name}" pip_lower)
        if (pip_lower STREQUAL pip_missing)
            set(pip_was_present TRUE)
        else()
            file(CREATE_LINK "${pip_source}" "${pip_fixture}/${pip_name}" COPY_ON_ERROR)
        endif()
    endforeach()
    if (pip_was_present)
        pip_expect_failure("${pip_fixture}" "dependency missing.*${pip_missing}")
    else()
        message(STATUS "SKIP missing-${pip_missing}: not imported by the selected SDK")
    endif()
endforeach()

# A complete but malformed image cannot be accepted merely because its filename matches.
set(pip_fixture "${pip_run}/invalid_pe")
file(MAKE_DIRECTORY "${pip_fixture}")
foreach(pip_source IN LISTS pip_reference)
    get_filename_component(pip_name "${pip_source}" NAME)
    if (pip_name STREQUAL "avcodec-61.dll")
        file(WRITE "${pip_fixture}/${pip_name}" "invalid PE image")
    else()
        file(CREATE_LINK "${pip_source}" "${pip_fixture}/${pip_name}" COPY_ON_ERROR)
    endif()
endforeach()
pip_expect_failure("${pip_fixture}" "not a PE image")

if (TARGET_POINTER_SIZE EQUAL 8)
    pip_expect_failure("${RUNTIME_DIR}" "PE architecture does not match target" 4)
else()
    pip_expect_failure("${RUNTIME_DIR}" "PE architecture does not match target" 8)
endif()

# Empty explicit roots must fail even when the host PATH contains the real Qt runtime.
set(pip_fixture "${pip_run}/empty")
file(MAKE_DIRECTORY "${pip_fixture}")
pip_expect_failure("${pip_fixture}" "dependency missing.*avcodec-61.dll")
message(STATUS "FFmpeg import closure and missing-dependency/PE/explicit-root checks passed")
