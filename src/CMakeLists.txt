cmake_minimum_required(VERSION 3.10)
project(xwmux)


# X11
find_package(X11 REQUIRED)
link_libraries(${X11_LIBRARIES})
include_directories(${X11_INCLUDE_DIR})

# Lib
include_directories(xwmux-ctl xwmux)

add_compile_options(-std=c++23 -Ofast -flto=auto -march=native -fomit-frame-pointer -Wall -Wextra -Wpedantic -DNDEBUG)
add_link_options()

add_executable(xwmux xwmux/main.cpp)
add_executable(xwmux-ctl xwmux-utils/xwmux-ctl.cpp)
