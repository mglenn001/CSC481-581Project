cmake_minimum_required(VERSION 3.10)

project(CSC481GameEngine)

set(CMAKE_CXX_STANDARD 17)

# SDL
find_package(SDL3 REQUIRED)
find_package(SDL3_image REQUIRED)

# ZeroMQ
find_package(PkgConfig REQUIRED)
pkg_check_modules(ZMQ REQUIRED libzmq)

find_path(CPPZMQ_INCLUDE_DIR zmq.hpp)

if(NOT CPPZMQ_INCLUDE_DIR)
    message(FATAL_ERROR "cppzmq not found")
endif()

# Game client
add_executable(
    main
    src/main.cpp
    src/Entity.cpp
    src/Physics.cpp
    src/Input.cpp
    src/Collision.cpp
    src/Scaling.cpp
    src/Timeline.cpp
    src/Networking.cpp
)

target_include_directories(
    main
    PRIVATE
    ${CPPZMQ_INCLUDE_DIR}
    ${ZMQ_INCLUDE_DIRS}
)

target_link_directories(
    main
    PRIVATE
    ${ZMQ_LIBRARY_DIRS}
)

target_link_libraries(
    main
    PRIVATE
    SDL3::SDL3
    SDL3_image::SDL3_image
    ${ZMQ_LIBRARIES}
)

# Headless server
add_executable(
    server
    src/server.cpp
)

target_include_directories(
    server
    PRIVATE
    ${CPPZMQ_INCLUDE_DIR}
    ${ZMQ_INCLUDE_DIRS}
)

target_link_directories(
    server
    PRIVATE
    ${ZMQ_LIBRARY_DIRS}
)

target_link_libraries(
    server
    PRIVATE
    ${ZMQ_LIBRARIES}
)

# Milestone 3 Sections C.2-C.4: headless performance benchmark (no SDL needed).
# Uses the same Networking.cpp as the game. Built with -O2 so timings are not
# distorted by an unoptimized (Debug) build.
add_executable(
    benchmark
    src/Benchmark.cpp
    src/Networking.cpp
)
 
target_include_directories(
    benchmark
    PRIVATE
    ${CPPZMQ_INCLUDE_DIR}
    ${ZMQ_INCLUDE_DIRS}
)
 
target_link_directories(
    benchmark
    PRIVATE
    ${ZMQ_LIBRARY_DIRS}
)
 
target_link_libraries(
    benchmark
    PRIVATE
    ${ZMQ_LIBRARIES}
)
 
target_compile_options(benchmark PRIVATE -O2)