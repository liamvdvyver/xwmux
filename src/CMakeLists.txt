cmake_minimum_required(VERSION 3.10)
project(xwmux)

set(CMAKE_CXX_COMPILER clang++)

message("Compiler Version: ${CMAKE_CXX_COMPILER} ${CMAKE_CXX_COMPILER_VERSION}")
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_compile_options(-std=c++23 -O3 -flto=auto -march=native -fomit-frame-pointer -Wall -Wextra -Wpedantic -DNDEBUG)
# add_compile_options(-std=c++23 -Og -flto=auto -march=native -fomit-frame-pointer -Wall -Wextra -Wpedantic -g)
add_link_options(-fuse-ld=lld)

# X11
find_package(X11 REQUIRED)
link_libraries(${X11_LIBRARIES})
include_directories(${X11_INCLUDE_DIR})

# Lib
include_directories(xwmux-ctl xwmux)
include_directories(xwmux xwmux)

file(GLOB SOURCES
    xwmux/*.cpp
    xwmux/*.h
)

add_executable(xwmux ${SOURCES})
add_executable(xwmux-ctl xwmux-utils/xwmux-ctl.cpp)
add_executable(xwmux-win xwmux-utils/xwmux-win.cpp)

install(TARGETS xwmux xwmux-ctl xwmux-win)
