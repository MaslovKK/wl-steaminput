cmake_minimum_required(VERSION 3.30)

project(wl-steaminput)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

include(FetchContent)

# jmpews/Dobby itself does not build against current glibc; this is the branch
# where the fixes landed.
FetchContent_Declare(
    dobby
    GIT_REPOSITORY https://github.com/JingMatrix/Dobby
    GIT_TAG 05a09ac6807a6bb1726350e40ea4b127c1c79809)
FetchContent_MakeAvailable(dobby)

find_package(PkgConfig REQUIRED)
pkg_check_modules(WAYLAND_CLIENT REQUIRED wayland-client)

add_library(wlsteaminput SHARED
    src/core/entry.cpp
    src/core/log.cpp
    src/steam/client.cpp
    src/steam/steam.cpp
    src/wayland/wayland.cpp)

target_include_directories(wlsteaminput PRIVATE src ${WAYLAND_CLIENT_INCLUDE_DIRS})
target_link_libraries(wlsteaminput PRIVATE dobby ${CMAKE_DL_LIBS})
target_compile_options(wlsteaminput PRIVATE -Wall -Wextra)
