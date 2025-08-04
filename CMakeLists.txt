
cmake_minimum_required(VERSION 3.10)
project(metad)

set(CMAKE_CXX_STANDARD 17)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(CMAKE_INSTALL_PREFIX "${CMAKE_SOURCE_DIR}/deploy/debug" CACHE PATH "Install path" FORCE)
endif ()
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    set(CMAKE_INSTALL_PREFIX "${CMAKE_SOURCE_DIR}/deploy/release" CACHE PATH "Install path" FORCE)
endif ()
if(CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
    set(CMAKE_INSTALL_PREFIX "${CMAKE_SOURCE_DIR}/deploy/RelWithDebInfo" CACHE PATH "Install path" FORCE)
endif ()

find_package(OpenSSL REQUIRED)

add_executable(metad
    main.cpp

    workers/deltametaworker.cpp
    workers/fullmetaworker.cpp
    net/netfunc.h
    net/netfunc.cpp
    exeptions/unaccaptable.h

    mainfunc.h mainfunc.cpp
    xmlfunc.h xmlfunc.cpp
    meta.h meta.cpp
    threaddatacontainer.h
    tstring.h
)

# Подключаем заголовки tinyxml2
include_directories(/usr/include)
target_link_libraries(metad PRIVATE tinyxml2 OpenSSL::SSL OpenSSL::Crypto)

# Подключаем дефайны
target_compile_definitions(metad PRIVATE BUFFSIZE=255)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_definitions(metad PRIVATE DEBUG_BUILD)
    target_compile_definitions(metad PRIVATE LOCAL_SOCKET)
    target_compile_definitions(metad PRIVATE ASAN_ON)
    target_compile_options(metad PRIVATE -fsanitize=address -fno-omit-frame-pointer -g -O1)
    target_link_options(metad PRIVATE -fsanitize=address)
endif ()

if(CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
    target_compile_definitions(metad PRIVATE DEBUG_BUILD)
endif ()

install(TARGETS metad
    DESTINATION ${CMAKE_INSTALL_PREFIX})


