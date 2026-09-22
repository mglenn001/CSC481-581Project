cmake_minimum_required(VERSION 3.10)

project(CSC481GameEngine)

set(CMAKE_CXX_STANDARD 17)

find_package(SDL3 REQUIRED)
find_package(SDL3_image REQUIRED)

add_executable(
    main
    src/main.cpp
    src/Entity.cpp
    src/Physics.cpp
    src/Input.cpp
    src/Collision.cpp
    src/Scaling.cpp
    src/Timeline.cpp
)

target_link_libraries(
    main
    PRIVATE
    SDL3::SDL3
    SDL3_image::SDL3_image
)