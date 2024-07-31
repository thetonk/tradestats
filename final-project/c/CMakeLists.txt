cmake_minimum_required(VERSION 3.10)
project(FinalProjectRTES VERSION 1.0)

set(EXECUTABLE_OUTPUT_PATH "bin")

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_C_COMPILER gcc)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON) #useful if using clangd
add_compile_options(-g -Wall -Wextra)
set(EXECUTABLE_NAME "main")
set(MA_INTERVAL "3" CACHE STRING "Moving average interval in minutes")
set(STATS_OUTPUT_DIR "out" CACHE STRING "Output directory path for statistics and logs (candles, moving averages, trade logs)")

if(CMAKE_TOOLCHAIN_FILE)
  include(${CMAKE_TOOLCHAIN_FILE})
  set(EXECUTABLE_NAME "main-armhf")
endif()

if(MA_INTERVAL)
  add_compile_definitions(MOVING_AVERAGE_INTERVAL_MINUTES=${MA_INTERVAL})
endif()
if(STATS_OUTPUT_DIR)
  add_compile_definitions(OUTPUT_DIRECTORY="${STATS_OUTPUT_DIR}")
endif()

message("Using compiler ${CMAKE_C_COMPILER}")
add_subdirectory("${PROJECT_SOURCE_DIR}/src")
add_executable("${EXECUTABLE_NAME}" "${PROJECT_SOURCE_DIR}/main.c")
target_include_directories("${EXECUTABLE_NAME}" PUBLIC "include")
target_link_libraries("${EXECUTABLE_NAME}" myLibs websockets pthread)
