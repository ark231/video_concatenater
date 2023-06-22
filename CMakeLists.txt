cmake_minimum_required(VERSION 3.5)

project(video_concatenater VERSION 0.1 LANGUAGES CXX)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets LinguistTools MultimediaWidgets Gui)

set(TS_FILES video_concatenater_ja_JP.ts)

set(PROJECT_SOURCES
    main.cpp
    mainwindow.cpp
    mainwindow.hpp
    mainwindow.ui
    processwidget.hpp
    processwidget.cpp
    processwidget.ui
    listdialog.hpp
    listdialog.cpp
    listdialog.ui
    timedialog.hpp
    timedialog.cpp
    timedialog.ui
    videoinfowidget.hpp
    videoinfowidget.cpp
    videoinfowidget.ui
    videoinfodialog.hpp
    videoinfodialog.cpp
    videoinfodialog.ui
    videoinfo_stream.cpp
    ${TS_FILES}
    main_resources.qrc
    $<$<PLATFORM_ID:Windows>:windows.rc>
)

qt_add_executable(video_concatenater
    MANUAL_FINALIZATION
    ${PROJECT_SOURCES}
)

qt_create_translation(QM_FILES ${CMAKE_SOURCE_DIR} ${TS_FILES})

find_package(fmt QUIET)

if(NOT ${fmt_FOUND})
    include(cmake/CPM.cmake)
    CPMAddPackage(
        NAME fmt
        GITHUB_REPOSITORY fmtlib/fmt
        GIT_TAG 8.1.1
    )
endif()

add_subdirectory(3rdparty)

target_link_libraries(video_concatenater PRIVATE
    Qt6::Widgets
    Qt6::MultimediaWidgets
    Qt6::Gui
    fmt

    qt_collapsible_section
)

target_compile_definitions(video_concatenater PRIVATE $<$<NOT:$<CONFIG:Debug>>:QT_NO_DEBUG_OUTPUT$<SEMICOLON>QT_NO_DEBUG>)
target_compile_options(video_concatenater PRIVATE
    $<$<CXX_COMPILER_ID:Clang>:-Wall -Wextra>
    $<$<CXX_COMPILER_ID:GNU>:-Wall -Wextra>
)

set_target_properties(video_concatenater PROPERTIES
    MACOSX_BUNDLE_GUI_IDENTIFIER my.example.com
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

qt_finalize_executable(video_concatenater)
