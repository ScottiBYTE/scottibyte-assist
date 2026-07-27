cmake_minimum_required(VERSION 3.22)

project(
  scottibyte-assist
  VERSION 0.1.0
  LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(
  Qt6
  REQUIRED COMPONENTS
    Core
    Gui
    Widgets
)

qt_add_executable(
  scottibyte-assist
  src/main.cpp
)

target_link_libraries(
  scottibyte-assist
  PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
)

install(
  TARGETS scottibyte-assist
  RUNTIME DESTINATION bin
)
