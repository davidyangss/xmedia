cmake_minimum_required(VERSION 2.8)
project(rtc_lab)

option(LIBEXECINFO_STANDALONE "if libexecinfo is standalone" OFF)
message("Option LIBEXECINFO_STANDALONE=${LIBEXECINFO_STANDALONE}")

if (CMAKE_VERSION VERSION_LESS "3.1")
    add_definitions(-std=c++11)
else ()
    set(CMAKE_CXX_STANDARD 11)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)
endif ()

set(CMAKE_VERBOSE_MAKEFILE yes)


add_definitions(-DRAPIDJSON_HAS_STDSTRING=1)
add_definitions(-DRAPIDJSON_BUILD_DOC=0)
add_definitions(-DRAPIDJSON_BUILD_EXAMPLES=0)
add_definitions(-DRAPIDJSON_BUILD_TESTS=0)
add_definitions(-DHAS_DTLS_WINDOW_SIZE=1)

include_directories(src/rapidjson)

include_directories(src)
include_directories(src/ev_socket)
include_directories(src/branch_stream)

include_directories(src/xcode/RTCLab)

find_package(PkgConfig REQUIRED)

pkg_check_modules(GIB REQUIRED glib-2.0)
pkg_check_modules(LIBEVENT REQUIRED libevent)
pkg_check_modules(OPUS REQUIRED opus)

include_directories(
        ${GLIB_INCLUDE_DIRS}
        ${LIBEVENT_INCLUDE_DIRS}
        ${OPUS_INCLUDE_DIRS}
)

link_directories(
        ${GLIB_LIBRARY_DIRS}
        ${LIBEVENT_LIBRARY_DIRS}
        ${OPUS_LIBRARY_DIRS}
)


add_executable(bypass_dump
        xcode/RTCLab/main.cpp
        src/Lab040-BStreamDump.cpp
        src/ev_socket/XSocket.cpp
        src/branch_stream/BStreamProto.cpp
        src/xtlv_file.cpp)

target_link_libraries(bypass_dump
        ${GLIB_LDFLAGS}
        ${LIBEVENT_LDFLAGS}
        ${OPUS_LDFLAGS}
        )