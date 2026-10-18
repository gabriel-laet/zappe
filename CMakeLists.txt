cmake_minimum_required(VERSION 3.21)

project(zappe VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Quick Network WebSockets)
find_package(X11 REQUIRED)

set(QML_FILES
  qml/Main.qml
  qml/HomeView.qml
  qml/ShelfRow.qml
  qml/PosterCard.qml
  qml/Backdrop.qml
  qml/DetailView.qml
  qml/SplashView.qml
  qml/FpsOverlay.qml
  qml/SideMenu.qml
  qml/SettingsView.qml
  qml/VolumeHud.qml
  qml/Toast.qml
  qml/LoginPrompt.qml
  qml/LoadingView.qml
  qml/Motion.qml
)

foreach(qml ${QML_FILES})
  get_filename_component(qml_name ${qml} NAME)
  set_source_files_properties(${qml} PROPERTIES QT_RESOURCE_ALIAS ${qml_name})
endforeach()
set_source_files_properties(qml/Motion.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_executable(zappe
  src/main.cpp
  src/branding.cpp
  src/catalog.cpp
  src/posters.cpp
  src/guide.cpp
  src/selfcheck.cpp
  src/logiccheck.cpp
  src/keymap.cpp
  src/display.cpp
  src/audio.cpp
  src/voice.cpp
  src/ota.cpp
  src/samsung.cpp
  src/chrome.cpp
  src/cdp.cpp
  src/logins.cpp
  src/mirror.cpp
  src/learn.cpp
  src/remote.cpp
  src/house.cpp
  src/xback.cpp
)

qt_add_qml_module(zappe
  URI Guide
  VERSION 1.0
  QML_FILES ${QML_FILES}
)

qt_add_resources(zappe "zappe_data"
  PREFIX "/zappe"
  FILES
    data/catalog.json
    data/logins.json
    data/mirrors.json
    data/atongx-map.json
)

target_include_directories(zappe PRIVATE src)
target_link_libraries(zappe PRIVATE Qt6::Core Qt6::Gui Qt6::Quick Qt6::Network Qt6::WebSockets X11::X11)
target_compile_options(zappe PRIVATE -Wall -Wextra -Wpedantic)

add_custom_command(TARGET zappe POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          ${CMAKE_CURRENT_SOURCE_DIR}/data/catalog.json
          $<TARGET_FILE_DIR:zappe>/catalog.json
  VERBATIM)

enable_testing()
add_test(NAME self-check COMMAND zappe --self-check)
set_tests_properties(self-check PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;ZAPPE_AUDIO_DISABLE=1;ZAPPE_HID_DISABLE=1"
)
add_test(NAME updater-script COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/check-updater.sh)
add_test(NAME kiosk-script COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/check-kiosk.sh)
