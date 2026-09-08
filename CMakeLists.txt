cmake_minimum_required(VERSION 3.10)
project(NoSteamWebHelper C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# MinGW specific flags for small binary
if(MINGW)
    set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -Os -s")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static-libgcc -static-libstdc++")
endif()

# Include config.c in the build
add_library(umpdc SHARED src/Library.c src/config.c)

target_link_libraries(umpdc PRIVATE user32 shell32 psapi shlwapi advapi32)

# Ensure output is named umpdc.dll (Steam auto-loads this)
set_target_properties(umpdc PROPERTIES PREFIX "")
set_target_properties(umpdc PROPERTIES OUTPUT_NAME "umpdc")
