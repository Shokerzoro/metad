
cmake_minimum_required(VERSION 3.10)
project(metad)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_INSTALL_PREFIX "${CMAKE_SOURCE_DIR}/deploy" CACHE PATH "Install path" FORCE)

find_package(OpenSSL REQUIRED)

add_executable(metad
    main.cpp
    workers/deltametaworker.cpp
    workers/fullmetaworker.cpp
    mainfunc.cpp
    xmlfunc.cpp
    meta.cpp
    threaddatacontainer.h
    tstring.h
        net/netfunc.h
        net/netfunc.cpp
        exeptions/unaccaptable.h
)

# Подключаем заголовки tinyxml2
include_directories(/usr/include)
target_link_libraries(metad PRIVATE tinyxml2 OpenSSL::SSL OpenSSL::Crypto)

# Подключаем дефайны
target_compile_definitions(metad PRIVATE BUFFSIZE=255)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_definitions(metad PRIVATE DEBUG_BUILD)
    target_compile_options(metad PRIVATE -fsanitize=address -fno-omit-frame-pointer -g -O1)
    target_link_options(metad PRIVATE -fsanitize=address)
endif ()

install(TARGETS metad
    DESTINATION ".")


