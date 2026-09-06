include(cmake/QHotkey.cmake)
include(cmake/myproto.cmake)

add_library(throned_sqlite3 STATIC
        3rdparty/SQLiteCpp/src/sqlite3.c
)
add_library(Throned::SQLite3 ALIAS throned_sqlite3)
target_include_directories(throned_sqlite3 PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/SQLiteCpp/include"
)
target_compile_definitions(throned_sqlite3 PUBLIC
        SQLITE_DQS=0
        SQLITE_DEFAULT_MEMSTATUS=0
        SQLITE_DEFAULT_WAL_SYNCHRONOUS=1
        SQLITE_LIKE_DOESNT_MATCH_BLOBS
        SQLITE_MAX_EXPR_DEPTH=0
        SQLITE_UNTESTABLE
        SQLITE_OMIT_DEPRECATED
        SQLITE_OMIT_SHARED_CACHE
        SQLITE_OMIT_PROGRESS_CALLBACK
        SQLITE_OMIT_AUTHORIZATION
        SQLITE_OMIT_LOAD_EXTENSION
        SQLITE_OMIT_COMPLETE
        SQLITE_OMIT_GET_TABLE
        SQLITE_OMIT_TRACE
        SQLITE_OMIT_UTF16
        SQLITE_OMIT_INCRBLOB
        SQLITE_OMIT_DESERIALIZE
        SQLITE_OMIT_JSON
)
target_link_libraries(throned_sqlite3 PUBLIC Threads::Threads ${CMAKE_DL_LIBS})

add_library(throned_sqlitecpp STATIC
        3rdparty/SQLiteCpp/src/Backup.cpp
        3rdparty/SQLiteCpp/src/Column.cpp
        3rdparty/SQLiteCpp/src/Database.cpp
        3rdparty/SQLiteCpp/src/Exception.cpp
        3rdparty/SQLiteCpp/src/Savepoint.cpp
        3rdparty/SQLiteCpp/src/Statement.cpp
        3rdparty/SQLiteCpp/src/Transaction.cpp
)
add_library(Throned::SQLiteCpp ALIAS throned_sqlitecpp)
target_include_directories(throned_sqlitecpp
        PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/SQLiteCpp/include"
        PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
)
target_link_libraries(throned_sqlitecpp PUBLIC Throned::SQLite3)

add_library(throned_quirc STATIC
        3rdparty/quirc/decode.c
        3rdparty/quirc/identify.c
        3rdparty/quirc/quirc.c
        3rdparty/quirc/version_db.c
)
add_library(Throned::Quirc ALIAS throned_quirc)
target_include_directories(throned_quirc PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/quirc"
)

add_library(throned_qrcodegen STATIC
        3rdparty/qrcodegen/qrcodegen.cpp
)
add_library(Throned::QrCodeGen ALIAS throned_qrcodegen)
target_include_directories(throned_qrcodegen PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/qrcodegen"
)

add_library(throned_qv2ray STATIC
        3rdparty/qv2ray/v2/proxy/QvProxyConfigurator.cpp
        3rdparty/qv2ray/v2/proxy/QvProxyConfigurator.hpp
        3rdparty/qv2ray/v2/ui/LogHighlighter.cpp
        3rdparty/qv2ray/v2/ui/LogHighlighter.hpp
        3rdparty/qv2ray/v2/ui/QvAutoCompleteTextEdit.cpp
        3rdparty/qv2ray/v2/ui/QvAutoCompleteTextEdit.hpp
)
add_library(Throned::Qv2ray ALIAS throned_qv2ray)
set_target_properties(throned_qv2ray PROPERTIES AUTOMOC ON)
target_include_directories(throned_qv2ray
        PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/qv2ray"
)
target_link_libraries(throned_qv2ray PUBLIC Qt6::Widgets Qt6::Network)
if (WIN32)
    target_link_libraries(throned_qv2ray PRIVATE wininet rasapi32)
endif ()

add_library(throned_fkyaml INTERFACE)
add_library(Throned::FkYAML ALIAS throned_fkyaml)
target_include_directories(throned_fkyaml INTERFACE
        "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty"
)

set(THRONED_DEPENDENCIES
        Throned::FkYAML
        Throned::Protocol
        Throned::QrCodeGen
        Throned::Quirc
        Throned::Qv2ray
        Throned::SQLiteCpp
        QHotkey::QHotkey
)

if (WIN32)
    add_library(throned_win_commander STATIC
            3rdparty/WinCommander/WinCommander.cpp
    )
    add_library(Throned::WinCommander ALIAS throned_win_commander)
    target_include_directories(throned_win_commander PUBLIC
            "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/WinCommander"
    )
    target_link_libraries(throned_win_commander PUBLIC Qt6::Core)
    list(APPEND THRONED_DEPENDENCIES Throned::WinCommander)
endif ()

if (MSVC)
    target_compile_options(throned_sqlite3 PRIVATE /W0)
    target_compile_options(throned_quirc PRIVATE /W0)
endif ()
