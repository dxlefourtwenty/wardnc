cmake_minimum_required(VERSION 3.16)
project(wardnc LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Core DBus Widgets)
find_package(LayerShellQt QUIET)

qt_add_executable(wardnc
    src/main.cpp
    src/NotificationServer.cpp
    src/WardNcControl.cpp
    src/WardNcConfig.cpp
    src/NotificationCenterPanel.cpp
)

target_link_libraries(wardnc PRIVATE
    Qt6::Core
    Qt6::DBus
    Qt6::Widgets
)

if(LayerShellQt_FOUND)
    target_link_libraries(wardnc PRIVATE LayerShellQt::Interface)
    target_compile_definitions(wardnc PRIVATE WARDNC_HAS_LAYERSHELLQT=1)
else()
    target_compile_definitions(wardnc PRIVATE WARDNC_HAS_LAYERSHELLQT=0)
endif()

install(TARGETS wardnc RUNTIME DESTINATION bin)
