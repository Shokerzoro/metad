
cmake_minimum_required(VERSION 3.10)
project(metad)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_INSTALL_PREFIX "${CMAKE_SOURCE_DIR}/deploy" CACHE PATH "Install path" FORCE)

add_executable(metad
    main.cpp
    workers/deltametaworker.cpp
    workers/fullmetaworker.cpp

    mainfunc.cpp
    xmlfunc.cpp
    meta.cpp

    threaddatacontainer.h
    tstring.h
)

# Подключаем заголовки tinyxml2
include_directories(/usr/include)

# Линкуем с libtinyxml2.so
target_link_libraries(metad PRIVATE tinyxml2)

# Подключаем дефайны
target_compile_definitions(metad PRIVATE BUFFSIZE=255)

install(TARGETS metad
    DESTINATION ".")


