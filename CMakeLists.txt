cmake_minimum_required(VERSION 3.20)

project(CS3103_Project C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Enable compiler warnings
add_compile_options(-Wall -Wextra)

# POSIX Threads
find_package(Threads REQUIRED)

# Problem 1: Producer-Consumer
add_executable(problem1 problem1.c)
set_target_properties(problem1 PROPERTIES C_EXTENSIONS ON)
target_link_libraries(problem1 PRIVATE Threads::Threads)

# Problem 2: Mini File System
add_executable(minifs problem2.c)
set_target_properties(minifs PROPERTIES C_EXTENSIONS OFF)


# Problem 3: CPU Scheduling
# Automatically add target when problem3.c exists
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/problem3.c")
    add_executable(problem3 problem3.c)
    set_target_properties(problem3 PROPERTIES C_EXTENSIONS OFF)
endif()
