
# CMake Toolchain file for crosscompiling on ARM.
#
# This can be used when running cmake in the following way:
#  cd build/
#  cmake .. -DCMAKE_TOOLCHAIN_FILE=../cross-arm-linux-gnueabihf.cmake
# In order for this toolchain to work, you must copy the directories under libc to the sysroot path
set(CROSS_PATH /home/spyros/Downloads/cross-pi-gcc-10.3.0-0 CACHE STRING "Cross compiler toolchain path")
set(SYSROOT ${CROSS_PATH}/arm-linux-gnueabihf CACHE STRING "Cross compiler sysroot path")
# Target operating system name.
set(CMAKE_SYSTEM_NAME Linux)
message(STATUS "SYSROOT: ${SYSROOT}")
# Name of C compiler.
set(CMAKE_C_COMPILER ${CROSS_PATH}/bin/arm-linux-gnueabihf-gcc)
set(CMAKE_CXX_COMPILER ${CROSS_PATH}/bin/arm-linux-gnueabihf-gcc)
set(CMAKE_RANLIB ${CROSS_PATH}/bin/arm-linux-gnueabihf-gcc-ranlib)
set(CMAKE_AR ${CROSS_PATH}/bin/arm-linux-gnueabihf-gcc-ar)
set(CMAKE_SYSROOT ${SYSROOT})
# Where to look for the target environment. (More paths can be added here)
set(CMAKE_FIND_ROOT_PATH  ${SYSROOOT})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
