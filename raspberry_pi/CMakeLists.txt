cmake_minimum_required(VERSION 3.16)

project(Interface-Coletivo)

set(CMAKE_CXX_STANDARD 17)

add_executable(coletivo
    src/main.cpp
)

target_link_libraries(coletivo
    gpiodcxx
    gpiod
    pthread
)