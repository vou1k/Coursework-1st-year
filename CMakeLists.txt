cmake_minimum_required(VERSION 3.29)
project(HELLOSFML)

set(CMAKE_CXX_STANDARD 14)

add_executable(HELLOSFML main.cpp)

set(SFML_STATIC_LIBRARIES TRUE)
set(SFML_DIR C:/sfml/lib/cmake/SFML)
find_package(SFML COMPONENTS system window graphics audio network REQUIRED)

include_directories(C:/sfml/include)
target_link_libraries(HELLOSFML sfml-system sfml-window sfml-graphics sfml-audio)