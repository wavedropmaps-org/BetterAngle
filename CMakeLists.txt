cmake_minimum_required(VERSION 3.16)
project(BetterAngle VERSION 5.5.153 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Quick QuickControls2 Widgets Gui)

set(PROJECT_SOURCES
    src/main_app/BetterAngle.cpp
    src/shared/ControlPanel.cpp
    include/shared/BetterAngleBackend.h
    src/shared/BetterAngleBackend.cpp
    src/shared/Detector.cpp
    src/shared/Input.cpp
    src/shared/Logic.cpp
    src/shared/Overlay.cpp
    src/shared/Profile.cpp
    src/shared/State.cpp
    src/shared/EnhancedLogging.cpp
    src/shared/Tray.cpp
    src/shared/Updater.cpp
    qml.qrc
    assets/resource.rc
)

add_executable(BetterAngle WIN32 ${PROJECT_SOURCES})

target_link_libraries(BetterAngle PRIVATE Qt6::Quick Qt6::QuickControls2 Qt6::Widgets Qt6::Gui gdi32 user32 shell32 ole32)

set_target_properties(BetterAngle PROPERTIES LINK_FLAGS "/MANIFESTUAC:\"level='requireAdministrator' uiAccess='false'\"")

target_include_directories(BetterAngle PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

# Release creation is handled by GitHub Actions workflow, not POST_BUILD
