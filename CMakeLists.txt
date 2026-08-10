cmake_minimum_required(VERSION 3.22)

project(
  scottibyte-assist
  VERSION 0.3.7
  LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

add_compile_definitions(
  SCOTTIBYTE_ASSIST_VERSION="${PROJECT_VERSION}"
)
set(CMAKE_AUTORCC ON)

find_package(
  Qt6
  REQUIRED COMPONENTS
    Core
    Gui
    Network
    WebSockets
    Widgets
)

find_package(PkgConfig REQUIRED)

pkg_check_modules(
  WEBRTC_APM
  REQUIRED
  IMPORTED_TARGET
  webrtc-audio-processing-1
)

set(
  ASSIST_COMMON_SOURCES
  assets.qrc
  src/main.cpp
  src/assist_audio_processor.cpp
  src/assist_audio_processor.h
  src/audio_devices.h
  src/customer_voice_audio.h
  src/desktop_backend.h
  src/lan_session.cpp
  src/lan_session.h
  src/remote_view.cpp
  src/remote_view.h
  src/vp8_video_codec.cpp
  src/vp8_video_codec.h
  src/wan_signaling_client.cpp
  src/wan_signaling_client.h
)

if(WIN32)
  find_package(PkgConfig REQUIRED)

  pkg_check_modules(
    GSTREAMER
    REQUIRED
    IMPORTED_TARGET
    gstreamer-1.0
    gstreamer-app-1.0
    gstreamer-audio-1.0
    gstreamer-rtp-1.0
  )

  set(
    ASSIST_PLATFORM_SOURCES
    src/windows_audio_devices.cpp
    src/windows_customer_voice_audio.cpp
    src/windows_desktop_backend.cpp
    src/windows_desktop_backend.h
  )
else()
  find_package(PkgConfig REQUIRED)
  find_package(X11 REQUIRED)

  find_package(
    Qt6
    REQUIRED COMPONENTS
      DBus
  )

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

  pkg_check_modules(
    GSTREAMER
    REQUIRED
    IMPORTED_TARGET
    gstreamer-1.0
    gstreamer-app-1.0
    gstreamer-audio-1.0
    gstreamer-rtp-1.0
  )

  find_library(
    XTST_LIBRARY
    Xtst
    REQUIRED
  )

  set(
    ASSIST_PLATFORM_SOURCES
    src/audio_devices.cpp
    src/customer_voice_audio.cpp
    src/libei_input.cpp
    src/libei_input.h
    src/pipewire_echo_cancel.cpp
    src/pipewire_echo_cancel.h
    src/pipewire_preview.cpp
    src/pipewire_preview.h
    src/portal_session.cpp
    src/portal_session.h
    src/wayland_desktop_backend.cpp
    src/wayland_desktop_backend.h
    src/x11_desktop_backend.cpp
    src/x11_desktop_backend.h
  )
endif()

qt_add_executable(
  scottibyte-assist
  ${ASSIST_COMMON_SOURCES}
  ${ASSIST_PLATFORM_SOURCES}
  src/wan_voice_relay.cpp
  src/wan_voice_relay.h
)

target_link_libraries(
  scottibyte-assist
  PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Network
    Qt6::WebSockets
    Qt6::Widgets
    PkgConfig::WEBRTC_APM
)

if(WIN32)
  set_target_properties(
    scottibyte-assist
    PROPERTIES
      WIN32_EXECUTABLE TRUE
  )

  target_link_libraries(
    scottibyte-assist
    PRIVATE
      PkgConfig::GSTREAMER
      ole32
      uuid
  )
else()
  target_link_libraries(
    scottibyte-assist
    PRIVATE
      Qt6::DBus
      PkgConfig::PIPEWIRE
      PkgConfig::LIBEI
      PkgConfig::GSTREAMER
      ole32
      uuid
      X11::X11
      ${XTST_LIBRARY}
  )
endif()

install(
  TARGETS scottibyte-assist
  RUNTIME DESTINATION bin
)

if(NOT WIN32)
  qt_add_executable(
    wan-signaling-test
    src/wan_signaling_test.cpp
    src/wan_signaling_client.cpp
    src/wan_signaling_client.h
  )

  target_link_libraries(
    wan-signaling-test
    PRIVATE
      Qt6::Core
      Qt6::Network
      Qt6::WebSockets
  )

  qt_add_executable(
    customer-voice-test
    src/customer_voice_test.cpp
    src/customer_voice_audio.cpp
    src/customer_voice_audio.h
  )

  target_link_libraries(
    customer-voice-test
    PRIVATE
      Qt6::Core
      PkgConfig::GSTREAMER
      ole32
      uuid
  )

  qt_add_executable(
    pipewire-echo-cancel-test
    src/pipewire_echo_cancel_test.cpp
    src/pipewire_echo_cancel.cpp
    src/pipewire_echo_cancel.h
  )

  target_link_libraries(
    pipewire-echo-cancel-test
    PRIVATE
      Qt6::Core
  )

endif()

qt_add_executable(
  vp8-video-codec-test
  src/vp8_video_codec_test.cpp
  src/vp8_video_codec.cpp
  src/vp8_video_codec.h
)

target_link_libraries(
  vp8-video-codec-test
  PRIVATE
    Qt6::Core
    Qt6::Gui
    PkgConfig::GSTREAMER
)

qt_add_executable(
  assist-aec-test
  src/assist_aec_test.cpp
  src/assist_audio_processor.cpp
  src/assist_audio_processor.h
)

target_link_libraries(
  assist-aec-test
  PRIVATE
    Qt6::Core
    PkgConfig::WEBRTC_APM
)
