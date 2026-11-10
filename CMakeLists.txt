cmake_minimum_required(VERSION 3.24)

project(PIPClient VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 6.5 REQUIRED COMPONENTS Quick)

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
)

# The application and offline tests consume the same generated C++ definitions.
add_library(pip_protocol STATIC
    proto/client_to_server.proto
    proto/server_to_client.proto
)

qt_add_qml_module(PIPClient
    URI PIPClient
    VERSION 1.0
    QML_FILES
        ui/Main.qml
)

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
add_library(pip_data_pipeline STATIC src/core/data_pipeline.cpp src/core/data_pipeline.h)
target_include_directories(pip_data_pipeline PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(pip_data_pipeline PUBLIC Qt6::Core pip_protocol PRIVATE Qt6::Mqtt)
target_link_libraries(PIPClient PRIVATE Qt6::Quick pip_data_pipeline)

include(CTest)
if (BUILD_TESTING)
    find_package(Qt6 ${Qt6_VERSION} EXACT REQUIRED COMPONENTS Test)
    add_executable(protocol_2027_test tests/protocol_2027_test.cpp)
    target_link_libraries(protocol_2027_test PRIVATE pip_protocol)
    add_test(NAME protocol_2027
        COMMAND protocol_2027_test "${CMAKE_CURRENT_SOURCE_DIR}/tests/protocol_2027_schema.txt")
    add_executable(data_pipeline_test tests/data_pipeline_test.cpp)
    target_link_libraries(data_pipeline_test PRIVATE pip_data_pipeline Qt6::Test Qt6::Network Qt6::Quick)
    target_compile_definitions(data_pipeline_test PRIVATE
        PIP_MAIN_QML_PATH="${CMAKE_CURRENT_SOURCE_DIR}/ui/Main.qml")
    add_test(NAME data_pipeline COMMAND data_pipeline_test)
    set_tests_properties(data_pipeline PROPERTIES TIMEOUT 40 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()

include(GNUInstallDirs)
install(TARGETS PIPClient
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
