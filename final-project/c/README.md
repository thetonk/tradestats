# TradeStats
Real time statistics for stocks, forex and crypto, using data from [FinnHub](https://finnhub.io/)!

## Compilation
> [!WARNING]
> OpenSSL version 3.0 or higher is required, otherwise compilation will fail!

> [!NOTE]
> This compilation process has been tested on linux x86 system and a raspberry pi 1 model B.

This application can be built using CMake. To build it follow the steps below:
1. Create a build directory and switch to it

`$ mkdir build && cd build`

2. Run CMake with your desired settings and compile. For example, to use the default settings, use the commands:

`$ cmake .. && make`

The executable then can be found under folder `bin`.

> [!IMPORTANT] Cross compilation for Raspberry Pi.
> In order to cross compile, you need to specify the `SYSROOT` and `CROSS_PATH` settings. You may call CMake like this: `cmake -DCMAKE_TOOLCHAIN_FILE="$(realpath ../cross-toolchain.cmake)" -DSYSROOT="path" -DCROSS_PATH="cross-path"` and then compile using `make`. Also, you may need to copy the directories under libc to the sysroot path.

### Optional settings
You may customize the program with the following optional settings:

| Option name|Default value|Example usage| Description|
|--|---|---|---|
|`MA_INTERVAL`|3| `-DMA_INTERVAL=15`|moving average calculation window, in minutes|
|`EXECUTABLE_NAME`|"main"| `-DEXECUTABLE_NAME="tradestats"`| name of the executable|
|`DEBUG`|OFF|`-DDEBUG=ON`|turn on/off debugging symbols, warnings and more verbose output|
|`STATS_OUTPUT_DIR`|"out"|`-DSTATS_OUTPUT_DIR="out"`|set the name of the folder which will contain the statistics|
| `CROSS_PATH`|"/home/spyros/Downloads/cross-pi-gcc-10.3.0-0 "|`-DCROSS_PATH="path"`|the cross compiler toolchain path|
|`SYSROOT`|"/home/spyros/Downloads/cross-pi-gcc-10.3.0-0/arm-linux-gnueabihf"|`-DSYSROOT="path"`|set the sysroot path|

## Usage
In order to run it, you will need your finnhub API token and a [symbols.txt](./symbols.txt) file just like the one I provided.

`$ <executable name> <your finnhub API token> <path-to-symbols.txt-file>`
