cmake_minimum_required(VERSION 3.13)
project(EUP C)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -m68000 -Os -Wall -Wno-pointer-sign -Wno-multichar -Wno-array-bounds -ffunction-sections -fdata-sections")
# version.h, for the .r file
list(APPEND REZ_INCLUDE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/../core)
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,--gc-sections")
add_application(EUP packager.c ../core/ini.c ../core/paths.c ../core/format.c ../core/deflate.c ../core/package.c packager.r CREATOR "EUPp")
