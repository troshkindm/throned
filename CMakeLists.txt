cmake_minimum_required(VERSION 3.20)

project(Throned VERSION 0.1)

# Ninja and MSVC must use the same VSLANG during configure and build.
if (CMAKE_GENERATOR MATCHES "Ninja" AND MSVC AND NOT "$ENV{VSLANG}" STREQUAL "1033")
    message(WARNING
        "Ninja/MSVC header tracking requires VSLANG=1033 for configure and build; "
        "use a Windows preset.")
endif()

set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    string(REPLACE "/Zi" "/Z7" CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO}")
    string(REPLACE "/Zi" "/Z7" CMAKE_C_FLAGS_RELWITHDEBINFO "${CMAKE_C_FLAGS_RELWITHDEBINFO}")
endif()

if (MSVC)
    add_link_options(
            $<$<NOT:$<CONFIG:Debug>>:/INCREMENTAL:NO>
            $<$<NOT:$<CONFIG:Debug>>:/OPT:REF>
            $<$<NOT:$<CONFIG:Debug>>:/OPT:ICF>
    )
else ()
    add_compile_options(-ffunction-sections -fdata-sections)
    if (APPLE)
        add_link_options(-Wl,-dead_strip)
    else ()
        add_link_options(-Wl,--gc-sections)
    endif ()
endif ()

if (UNIX AND NOT APPLE)
    set(QT_DBUS_COMPONENT DBus)
    set(QT_DBUS_LIBRARY Qt6::DBus)
endif ()

option(THRONED_UNITY_BUILD "Compile the app as unity batches" ON)
option(THRONED_PRECOMPILE_HEADERS "Precompile the common Qt headers" ON)

# Must precede FetchContent and add_subdirectory.
set(BUILD_SHARED_LIBS OFF)

if (NOT CMAKE_CXX_COMPILER_LAUNCHER)
    find_program(THRONED_COMPILER_CACHE NAMES sccache ccache)
    if (THRONED_COMPILER_CACHE)
        set(CMAKE_C_COMPILER_LAUNCHER "${THRONED_COMPILER_CACHE}")
        set(CMAKE_CXX_COMPILER_LAUNCHER "${THRONED_COMPILER_CACHE}")
        message(STATUS "Using compiler cache: ${THRONED_COMPILER_CACHE}")
    endif ()
endif ()

find_package(Qt6 REQUIRED COMPONENTS Widgets Network Svg LinguistTools ${QT_DBUS_COMPONENT})
find_package(Qt6 QUIET COMPONENTS Test)

# Make QWindowKit's optional private Qt dependencies visible to qt_finalize_executable.
set(QT_NO_PRIVATE_MODULE_WARNING ON)
foreach (QT_PRIVATE_MODULE Core Gui Widgets)
    find_package(Qt6${QT_PRIVATE_MODULE}Private QUIET)
endforeach ()
unset(QT_NO_PRIVATE_MODULE_WARNING)

include(FetchContent)
find_package(Git REQUIRED)
set(QWINDOWKIT_BUILD_STATIC ON CACHE BOOL "" FORCE)
set(QWINDOWKIT_BUILD_WIDGETS ON CACHE BOOL "" FORCE)
set(QWINDOWKIT_BUILD_QUICK OFF CACHE BOOL "" FORCE)
set(QWINDOWKIT_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(QWINDOWKIT_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(QWINDOWKIT_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(qwindowkit
        GIT_REPOSITORY https://github.com/stdware/qwindowkit.git
        GIT_TAG 4f683f2e0e4f3a7d6061d0224de9ed30e7c17e0f
        GIT_SUBMODULES_RECURSE ON
        PATCH_COMMAND
            ${CMAKE_COMMAND}
            "-DGIT_EXECUTABLE=${GIT_EXECUTABLE}"
            "-DPATCH_SOURCE_DIR=<SOURCE_DIR>"
            "-DPATCH_FILE=${CMAKE_CURRENT_SOURCE_DIR}/cmake/patches/qwindowkit-qt62-wayland.patch"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/ApplyGitPatch.cmake")
FetchContent_MakeAvailable(qwindowkit)

include("cmake/nkr.cmake")

if (APPLE)
    include("cmake/macos/macos.cmake")
elseif (WIN32)
    include("cmake/windows/windows.cmake")
else ()
    include("cmake/linux/linux.cmake")
endif ()

if (NOT MSVC)
    add_compile_options(-fno-omit-frame-pointer)
endif()

add_compile_definitions(QT_MESSAGELOGCONTEXT)

if (CMAKE_CROSSCOMPILING)
    list(APPEND CMAKE_FIND_ROOT_PATH ${CMAKE_PREFIX_PATH})
endif ()

find_package(Threads REQUIRED)

if (NKR_PACKAGE OR NKR_PACKAGE_MACOS)
    nkr_add_compile_definitions(NKR_CPP_USE_APPDATA)
endif ()

include("cmake/ThronedDependencies.cmake")

include(cmake/ThronedSources.cmake)

qt_add_executable(Throned
        MANUAL_FINALIZATION
        ${PROJECT_SOURCES}
)

if (MSVC)
    target_compile_options(Throned PRIVATE /W3 /wd4996)
endif ()

set_property(TARGET Throned PROPERTY AUTOUIC ON)
set_property(TARGET Throned PROPERTY AUTOMOC ON)
set_property(TARGET Throned PROPERTY AUTORCC ON)


set(MACOSX_ICON ${CMAKE_SOURCE_DIR}/res/Throned.icns)
if (APPLE)
    target_sources(Throned PRIVATE ${MACOSX_ICON})
endif ()

set_target_properties(Throned PROPERTIES
        MACOSX_BUNDLE_ICON_FILE "Throned.icns"
        RESOURCE ${MACOSX_ICON}
        MACOSX_BUNDLE_INFO_PLIST ${CMAKE_SOURCE_DIR}/res/MacOSXBundleInfo.plist
        MACOSX_BUNDLE_GUI_IDENTIFIER app.throned.desktop
        MACOSX_BUNDLE TRUE
        WIN32_EXECUTABLE TRUE
        UNITY_BUILD ${THRONED_UNITY_BUILD}
)

set(TS_FILES
        res/translations/zh_CN.ts
        res/translations/fa_IR.ts
        res/translations/ru_RU.ts
        res/translations/en_US.ts
)
set(LUPDATE_OPTIONS
        -locations none -no-obsolete
)
qt_add_lupdate(Throned TS_FILES ${TS_FILES} OPTIONS ${LUPDATE_OPTIONS})
qt_add_lrelease(Throned TS_FILES ${TS_FILES} QM_FILES_OUTPUT_VARIABLE QM_FILES)

foreach (QT_QM qtbase_ru qtbase_zh_CN qtbase_fa)
    configure_file(res/translations/${QT_QM}.qm ${CMAKE_BINARY_DIR}/${QT_QM}.qm COPYONLY)
endforeach ()
configure_file(res/translations/translations.qrc ${CMAKE_BINARY_DIR} COPYONLY)
target_sources(Throned PRIVATE ${CMAKE_BINARY_DIR}/translations.qrc)

target_link_libraries(Throned PRIVATE
        Qt6::Widgets Qt6::Network Qt6::Svg ${QT_DBUS_LIBRARY}
        QWindowKit::Widgets
        Threads::Threads
        ${THRONED_DEPENDENCIES}
        ${PLATFORM_LIBRARIES}
)

if (THRONED_PRECOMPILE_HEADERS)
    target_precompile_headers(Throned PRIVATE
            "$<$<COMPILE_LANGUAGE:CXX>:${CMAKE_CURRENT_SOURCE_DIR}/include/global/PrecompiledHeaders.hpp>")
endif ()

qt_finalize_executable(Throned)

qt_add_executable(throned_snapshot_compare tests/snapshot_compare.cpp)
set_target_properties(throned_snapshot_compare PROPERTIES UNITY_BUILD OFF)
target_link_libraries(throned_snapshot_compare PRIVATE Qt6::Gui)

add_custom_target(ui-smoke
    COMMAND ${CMAKE_COMMAND}
        "-DTHRONED_EXECUTABLE=$<TARGET_FILE:Throned>"
        "-DSNAPSHOT_COMPARE=$<TARGET_FILE:throned_snapshot_compare>"
        "-DQT_RUNTIME_DIR=$<TARGET_FILE_DIR:Qt6::qmake>"
        -DSCENARIOS=smoke
        -P "${CMAKE_CURRENT_SOURCE_DIR}/script/run_ui_scenarios.cmake"
    DEPENDS Throned throned_snapshot_compare
    USES_TERMINAL
)

add_custom_target(ui-all
    COMMAND ${CMAKE_COMMAND}
        "-DTHRONED_EXECUTABLE=$<TARGET_FILE:Throned>"
        "-DSNAPSHOT_COMPARE=$<TARGET_FILE:throned_snapshot_compare>"
        "-DQT_RUNTIME_DIR=$<TARGET_FILE_DIR:Qt6::qmake>"
        -DSCENARIOS=all
        -P "${CMAKE_CURRENT_SOURCE_DIR}/script/run_ui_scenarios.cmake"
    DEPENDS Throned throned_snapshot_compare
    USES_TERMINAL
)

add_custom_target(ui-update-baselines
    COMMAND ${CMAKE_COMMAND}
        "-DTHRONED_EXECUTABLE=$<TARGET_FILE:Throned>"
        "-DSNAPSHOT_COMPARE=$<TARGET_FILE:throned_snapshot_compare>"
        "-DQT_RUNTIME_DIR=$<TARGET_FILE_DIR:Qt6::qmake>"
        -DSCENARIOS=smoke
        -DUPDATE_BASELINES=ON
        -P "${CMAKE_CURRENT_SOURCE_DIR}/script/run_ui_scenarios.cmake"
    DEPENDS Throned throned_snapshot_compare
    USES_TERMINAL
)

if (Qt6Test_FOUND)
    enable_testing()
    add_subdirectory(tests)
endif ()
