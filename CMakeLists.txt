cmake_minimum_required(VERSION 3.17)
project(Bank)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_EXE_LINKER_FLAGS "-static -static-libgcc")
set(SFML_STATIC_LIBRARIES)

add_executable(HELLOSFML WIN32 main.cpp)

set(SFML_STATIC_LIBRARIES TRUE)
set(SFML_DIR C:/sfml/lib/cmake/SFML)
find_package(SFML COMPONENTS system window graphics audio network REQUIRED)

include_directories(C:/sfml/include)
target_link_libraries(HELLOSFML sfml-system sfml-window sfml-graphics sfml-audio)