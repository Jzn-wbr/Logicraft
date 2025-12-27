cmake_minimum_required(VERSION 3.20)

project(logicraft LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(SDL2 CONFIG REQUIRED)
find_package(GLEW REQUIRED)
find_package(OpenGL REQUIRED)

add_executable(logicraft
  src/main.cpp
  src/render.cpp
  src/world.cpp
)

target_link_libraries(logicraft PRIVATE
  SDL2::SDL2
  GLEW::GLEW
  OpenGL::GL
  OpenGL::GLU
)

if(TARGET SDL2::SDL2main)
  target_link_libraries(logicraft PRIVATE SDL2::SDL2main)
endif()

target_include_directories(logicraft PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)
