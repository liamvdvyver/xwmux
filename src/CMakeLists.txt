cmake_minimum_required(VERSION 3.10)
project(xwmux)

message("Compiler Version: ${CMAKE_CXX_COMPILER} ${CMAKE_CXX_COMPILER_VERSION}")
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_compile_options(-std=c++23 -O3 -Wall -Wextra -Wpedantic -DNDEBUG)

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

file(GLOB SCRIPTS
    scripts/*.sh
)

add_executable(xwmux ${SOURCES})
add_executable(xwmux-ctl xwmux-utils/xwmux-ctl.cpp)

install(TARGETS xwmux xwmux-ctl)
install(PROGRAMS ${SCRIPTS} TYPE BIN)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(CMAKE_EXPORT_COMPILE_COMMANDS)
  set(CMAKE_CXX_STANDARD_INCLUDE_DIRECTORIES 
      ${CMAKE_CXX_IMPLICIT_INCLUDE_DIRECTORIES})
endif()

