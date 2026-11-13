cmake_minimum_required(VERSION 3.25)

project(PIPIconRenderer LANGUAGES CXX)

# This authoring helper is independent of application dependencies and is never run by normal builds.
find_package(Qt6 6.12.0 EXACT REQUIRED COMPONENTS Gui Svg)
add_executable(pip_icon_renderer main.cpp)
target_compile_features(pip_icon_renderer PRIVATE cxx_std_20)
target_link_libraries(pip_icon_renderer PRIVATE Qt6::Gui Qt6::Svg)
