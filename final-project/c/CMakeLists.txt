cmake_minimum_required(VERSION 3.10)
project(FinalProjectRTES VERSION 1.0)

set(EXECUTABLE_OUTPUT_PATH "bin")

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_C_COMPILER gcc)

if(CMAKE_TOOLCHAIN_FILE)
  include(${CMAKE_TOOLCHAIN_FILE})
endif()
message("Using compiler ${CMAKE_C_COMPILER}")
add_subdirectory("${PROJECT_SOURCE_DIR}/src")
add_executable(main "${PROJECT_SOURCE_DIR}/main.c")
target_include_directories(main PUBLIC "include")
target_link_libraries(main myLibs websockets pthread)
