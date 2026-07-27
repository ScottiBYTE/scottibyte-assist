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
find_package(PkgConfig REQUIRED)

pkg_check_modules(
  PIPEWIRE
  REQUIRED
  IMPORTED_TARGET
  libpipewire-0.3
  libspa-0.2
)

pkg_check_modules(
  LIBEI
  REQUIRED
  IMPORTED_TARGET
  libei-1.0
)

find_library(
  XTST_LIBRARY
  Xtst
  REQUIRED
)

find_package(
  Qt6
  REQUIRED COMPONENTS
    Core
    DBus
    Gui
    Network
    Widgets
)

qt_add_executable(
  scottibyte-assist
  assets.qrc
  src/main.cpp
  src/desktop_backend.h
  src/lan_session.cpp
  src/lan_session.h
  src/libei_input.cpp
  src/libei_input.h
  src/pipewire_preview.cpp
  src/pipewire_preview.h
  src/portal_session.cpp
  src/portal_session.h
  src/remote_view.cpp
  src/remote_view.h
  src/wayland_desktop_backend.cpp
  src/wayland_desktop_backend.h
  src/x11_desktop_backend.cpp
  src/x11_desktop_backend.h
)

target_link_libraries(
  scottibyte-assist
  PRIVATE
    Qt6::Core
    Qt6::DBus
    Qt6::Gui
    Qt6::Network
    Qt6::Widgets
    PkgConfig::PIPEWIRE
    PkgConfig::LIBEI
    X11::X11
    ${XTST_LIBRARY}
)

install(
  TARGETS scottibyte-assist
  RUNTIME DESTINATION bin
)
