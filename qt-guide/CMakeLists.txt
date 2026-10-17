cmake_minimum_required(VERSION 3.21)

project(zappe-qt-guide VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Quick)

set(QML_FILES
  qml/Main.qml
  qml/HomeView.qml
  qml/ShelfRow.qml
  qml/PosterCard.qml
  qml/Backdrop.qml
  qml/DetailView.qml
  qml/SplashView.qml
  qml/FpsOverlay.qml
)

foreach(qml ${QML_FILES})
  get_filename_component(qml_name ${qml} NAME)
  set_source_files_properties(${qml} PROPERTIES QT_RESOURCE_ALIAS ${qml_name})
endforeach()

qt_add_executable(zappe-qt-guide
  src/main.cpp
  src/branding.cpp
  src/catalog.cpp
  src/posters.cpp
  src/guide.cpp
  src/selfcheck.cpp
)

qt_add_qml_module(zappe-qt-guide
  URI Guide
  VERSION 1.0
  QML_FILES ${QML_FILES}
)

qt_add_resources(zappe-qt-guide "sample_catalog"
  PREFIX "/zappe"
  FILES data/catalog.json
)

target_include_directories(zappe-qt-guide PRIVATE src)

target_link_libraries(zappe-qt-guide PRIVATE Qt6::Core Qt6::Gui Qt6::Quick)

target_compile_options(zappe-qt-guide PRIVATE -Wall -Wextra -Wpedantic)

add_custom_command(TARGET zappe-qt-guide POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          ${CMAKE_CURRENT_SOURCE_DIR}/data/catalog.json
          $<TARGET_FILE_DIR:zappe-qt-guide>/catalog.json
  VERBATIM)

enable_testing()
add_test(NAME self-check COMMAND zappe-qt-guide --self-check)
set_tests_properties(self-check PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)
