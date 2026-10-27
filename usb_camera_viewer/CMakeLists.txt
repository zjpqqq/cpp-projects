cmake_minimum_required(VERSION 3.16)
project(UsbCameraViewer VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets Multimedia)

qt_add_executable(usb_camera_viewer main.cpp)
target_link_libraries(usb_camera_viewer PRIVATE
    Qt6::Widgets
    Qt6::Multimedia
)

if(MSVC)
    target_compile_options(usb_camera_viewer PRIVATE /utf-8)
else()
    target_compile_options(usb_camera_viewer PRIVATE -Wall -Wextra -Wpedantic)
endif()
