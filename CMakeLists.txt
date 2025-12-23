cmake_minimum_required(VERSION 3.16)
project(mx44 C)

# Use pkg-config to find GTK4
find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK4 REQUIRED gtk4)

# Make GTK include dirs and libs available
include_directories(${GTK4_INCLUDE_DIRS})
link_directories(${GTK4_LIBRARY_DIRS})
add_definitions(${GTK4_CFLAGS_OTHER})

set(SRC_DIR "${CMAKE_SOURCE_DIR}/src")
file(GLOB SRC_FILES "${SRC_DIR}/*.c")

add_executable(mx44 ${SRC_FILES})

# apply compile options and link libraries
target_compile_options(mx44 PRIVATE ${GTK4_CFLAGS})
target_link_libraries(mx44 PRIVATE ${GTK4_LIBRARIES})
