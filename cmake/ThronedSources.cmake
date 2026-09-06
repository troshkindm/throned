file(GLOB_RECURSE THRONED_OWN_SOURCES
        CONFIGURE_DEPENDS
        LIST_DIRECTORIES FALSE
        RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/include/*.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/include/*.hpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/include/*.ui"
)

list(FILTER THRONED_OWN_SOURCES EXCLUDE REGEX
        [[^(src|include)/sys/(windows|linux|macos)/]])

set(THRONED_RESOURCE_SOURCES
        res/Throned.qrc
        ${QV2RAY_RC}
)

if (NOT APPLE AND Qt6_VERSION VERSION_GREATER_EQUAL 6.9.0)
    list(APPEND THRONED_RESOURCE_SOURCES res/EmojiFont.qrc)
    if (WIN32)
        list(APPEND THRONED_RESOURCE_SOURCES res/EmojiFontWin.qrc)
    endif ()

    set_property(SOURCE res/EmojiFont.qrc res/EmojiFontWin.qrc
            PROPERTY AUTORCC_OPTIONS "--compress-algo;zlib;--compress;9;--threshold;5")
endif ()

file(GLOB_RECURSE DASHBOARD_FILES
        CONFIGURE_DEPENDS
        LIST_DIRECTORIES FALSE
        RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}/res/dashboard"
        "${CMAKE_CURRENT_SOURCE_DIR}/res/dashboard/*"
)
list(FILTER DASHBOARD_FILES EXCLUDE REGEX [[(^|/)\.]])

if (DASHBOARD_FILES)
    set(DASHBOARD_QRC_ENTRIES "")
    foreach (DASHBOARD_FILE IN LISTS DASHBOARD_FILES)
        string(APPEND DASHBOARD_QRC_ENTRIES
                "        <file alias=\"${DASHBOARD_FILE}\">${CMAKE_CURRENT_SOURCE_DIR}/res/dashboard/${DASHBOARD_FILE}</file>\n")
    endforeach ()

    set(DASHBOARD_QRC "${CMAKE_CURRENT_BINARY_DIR}/dashboard.qrc")
    file(WRITE "${DASHBOARD_QRC}"
            "<RCC>\n    <qresource prefix=\"/dashboard\">\n${DASHBOARD_QRC_ENTRIES}    </qresource>\n</RCC>\n")
    list(APPEND THRONED_RESOURCE_SOURCES "${DASHBOARD_QRC}")
    set_property(SOURCE "${DASHBOARD_QRC}"
            PROPERTY AUTORCC_OPTIONS "--compress-algo;zlib;--compress;9;--threshold;5")
endif ()

set(PROJECT_SOURCES
        ${THRONED_OWN_SOURCES}
        ${PLATFORM_SOURCES}
        ${THRONED_RESOURCE_SOURCES}
)
