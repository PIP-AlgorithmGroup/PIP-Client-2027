cmake_minimum_required(VERSION 3.24)

project(PIPClient VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 6.5 REQUIRED COMPONENTS Quick)

qt_standard_project_setup(REQUIRES 6.5)
qt_policy(SET QTP0004 NEW)

qt_add_executable(PIPClient
    src/main.cpp
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

target_link_libraries(PIPClient PRIVATE Qt6::Quick)

include(GNUInstallDirs)
install(TARGETS PIPClient
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
