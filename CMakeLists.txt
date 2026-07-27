cmake_minimum_required(VERSION 3.22)

project(
  scottibyte-assist
  VERSION 0.1.0
  LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(X11 REQUIRED)

find_library(
  XTST_LIBRARY
  Xtst
  REQUIRED
)

find_package(
  Qt6
  REQUIRED COMPONENTS
    Core
    Gui
    Network
    Widgets
)

qt_add_executable(
  scottibyte-assist
  assets.qrc
  src/main.cpp
  src/lan_session.cpp
  src/lan_session.h
  src/remote_view.cpp
  src/remote_view.h
)

target_link_libraries(
  scottibyte-assist
  PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Network
    Qt6::Widgets
    X11::X11
    ${XTST_LIBRARY}
)

install(
  TARGETS scottibyte-assist
  RUNTIME DESTINATION bin
)
