cmake_minimum_required(VERSION 3.10)
project(fsview)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt5Widgets CONFIG REQUIRED)

set(libfsview_SRCS
    treemap.cpp
    fsview.cpp
    scan.cpp
    inode.cpp
    )
set(fsview_SRCS main.cpp ${libfsview_SRCS} )
add_executable(fsview ${fsview_SRCS})
target_link_libraries(fsview PUBLIC Qt5::Widgets)
install(TARGETS fsview)
