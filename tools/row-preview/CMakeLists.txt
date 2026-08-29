cmake_minimum_required(VERSION 3.20)

project(ThronedRowPreview LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets Network)

# The shipped delegate and theme, not copies: a change here is a change in the app.
qt_add_executable(ThronedRowPreview MANUAL_FINALIZATION
    main.cpp
    ../../include/ui/utils/ProfileRowDelegate.h
    ../../src/ui/utils/ProfileRowDelegate.cpp
    ../../include/ui/setting/ThemeManager.hpp
    ../../src/ui/setting/ThemeManager.cpp
)

target_link_libraries(ThronedRowPreview PRIVATE Qt6::Widgets Qt6::Network)
target_include_directories(ThronedRowPreview PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../..")

set_target_properties(ThronedRowPreview PROPERTIES OUTPUT_NAME "row-preview")

qt_finalize_executable(ThronedRowPreview)
