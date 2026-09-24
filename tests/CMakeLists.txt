# SettingsRepo defaults url_scheme_auto_register from the platform UrlScheme, so every target compiling it needs that source.
function(throned_test_needs_url_scheme TARGET_NAME)
    if (WIN32)
        target_sources(${TARGET_NAME} PRIVATE ${PROJECT_SOURCE_DIR}/src/sys/windows/UrlScheme.cpp)
    elseif (APPLE)
        target_sources(${TARGET_NAME} PRIVATE ${PROJECT_SOURCE_DIR}/src/sys/macos/UrlScheme.cpp)
    else ()
        target_sources(${TARGET_NAME} PRIVATE ${PROJECT_SOURCE_DIR}/src/sys/linux/UrlScheme.cpp)
    endif ()
endfunction()

function(throned_add_qt_test TARGET_NAME)
    set(options OFFSCREEN)
    set(one_value_args TIMEOUT)
    set(multi_value_args SOURCES LIBRARIES)
    cmake_parse_arguments(PARSE_ARGV 1 TEST
            "${options}" "${one_value_args}" "${multi_value_args}")

    qt_add_executable(${TARGET_NAME} ${TEST_SOURCES})
    set_target_properties(${TARGET_NAME} PROPERTIES
            AUTOMOC ON
            UNITY_BUILD OFF
    )
    target_include_directories(${TARGET_NAME} PRIVATE
            "${PROJECT_SOURCE_DIR}"
            "${PROJECT_BINARY_DIR}"
    )
    target_link_libraries(${TARGET_NAME} PRIVATE Qt6::Test ${TEST_LIBRARIES})

    if (TEST_OFFSCREEN)
        qt_import_plugins(${TARGET_NAME} INCLUDE Qt6::QOffscreenIntegrationPlugin)
        add_test(NAME ${TARGET_NAME} COMMAND ${TARGET_NAME} -platform offscreen)
    else ()
        add_test(NAME ${TARGET_NAME} COMMAND ${TARGET_NAME})
    endif ()

    if (TEST_TIMEOUT)
        set_tests_properties(${TARGET_NAME} PROPERTIES TIMEOUT ${TEST_TIMEOUT})
    endif ()
endfunction()

throned_add_qt_test(throned_window_notices_tests
        OFFSCREEN
        TIMEOUT 30
        SOURCES
            test_window_notices.cpp
            ${PROJECT_SOURCE_DIR}/src/ui/widget/WindowNotices.cpp
            ${PROJECT_SOURCE_DIR}/src/ui/widget/PendingRestartNotice.cpp
            ${PROJECT_SOURCE_DIR}/src/ui/widget/HijackDeprecationNotice.cpp
            ${PROJECT_SOURCE_DIR}/src/database/SettingsRepo.cpp
            ${PROJECT_SOURCE_DIR}/src/database/MarkersRepo.cpp
            ${PROJECT_SOURCE_DIR}/src/database/Database.cpp
            ${PROJECT_SOURCE_DIR}/src/ui/widget/UpdateStatusWidget.cpp
            ${PROJECT_SOURCE_DIR}/include/ui/widget/UpdateStatusWidget.h
            ${PROJECT_SOURCE_DIR}/src/ui/widget/MaterialIcon.cpp
            ${PROJECT_SOURCE_DIR}/include/ui/setting/ThemeManager.hpp
        LIBRARIES Qt6::Widgets Qt6::Network Qt6::Svg
            Throned::FkYAML Throned::Protocol Throned::SQLiteCpp
)
throned_test_needs_url_scheme(throned_window_notices_tests)
if (WIN32)
    target_sources(throned_window_notices_tests PRIVATE ${PROJECT_SOURCE_DIR}/src/sys/windows/WinVersion.cpp)
    target_link_libraries(throned_window_notices_tests PRIVATE ntdll)
    add_test(NAME throned_mica_theme_change
        COMMAND Throned -ui-preview ${PROJECT_BINARY_DIR}/mica-theme-cycle
            -ui-preview-theme-cycle -theme "Mica (Windows 11)")
    # WIN32_EXECUTABLE has no console, so Qt sends a failed assertion to the debugger unless forced.
    set_tests_properties(throned_mica_theme_change PROPERTIES TIMEOUT 30 SKIP_RETURN_CODE 77
        ENVIRONMENT "QT_FORCE_STDERR_LOGGING=1")
    add_test(NAME throned_mica_activation
        COMMAND Throned -ui-preview ${PROJECT_BINARY_DIR}/mica-activation
            -ui-preview-backdrop -ui-preview-mica-activation -theme "Mica (Windows 11)")
    set_tests_properties(throned_mica_activation PROPERTIES TIMEOUT 30 SKIP_RETURN_CODE 77
        ENVIRONMENT "QT_FORCE_STDERR_LOGGING=1")
endif ()

throned_add_qt_test(throned_diagnostics_tests
        OFFSCREEN
        TIMEOUT 30
        SOURCES
            test_diagnostics.cpp
            ${PROJECT_SOURCE_DIR}/src/ui/stats/diagnostics_window.cpp
            ${PROJECT_SOURCE_DIR}/include/ui/stats/diagnostics_window.h
            ${PROJECT_SOURCE_DIR}/src/ui/widget/MaterialIcon.cpp
            ${PROJECT_SOURCE_DIR}/src/ui/widget/ThronedTitleBar.cpp
            ${PROJECT_SOURCE_DIR}/src/ui/stats/TrafficChartWidget.cpp
            ${PROJECT_SOURCE_DIR}/include/ui/stats/TrafficChartWidget.h
            ${PROJECT_SOURCE_DIR}/include/ui/setting/ThemeManager.hpp
        LIBRARIES Qt6::Widgets Qt6::Network Qt6::Svg
            Throned::FkYAML Throned::Protocol Throned::SQLiteCpp
)

throned_add_qt_test(throned_traffic_stats_tests
        TIMEOUT 30
        SOURCES
            test_traffic_stats.cpp
            ${PROJECT_SOURCE_DIR}/src/database/TrafficStatsRepo.cpp
            ${PROJECT_SOURCE_DIR}/include/database/TrafficStatsRepo.h
            ${PROJECT_SOURCE_DIR}/src/database/Database.cpp
        LIBRARIES Qt6::Widgets Throned::SQLiteCpp
)

throned_add_qt_test(throned_site_reachability_tests
        OFFSCREEN
        TIMEOUT 30
        SOURCES
            test_site_reachability.cpp
            ${PROJECT_SOURCE_DIR}/src/ui/stats/dialog_site_reachability.cpp
            ${PROJECT_SOURCE_DIR}/include/ui/stats/dialog_site_reachability.h
            ${PROJECT_SOURCE_DIR}/include/ui/setting/ThemeManager.hpp
        LIBRARIES Qt6::Widgets Qt6::Network
            Throned::FkYAML Throned::Protocol Throned::SQLiteCpp
)

throned_add_qt_test(throned_tests
        SOURCES test_validate.cpp ${PROJECT_SOURCE_DIR}/src/configs/validate.cpp)

throned_add_qt_test(throned_route_tests
        SOURCES test_route_rule.cpp ${PROJECT_SOURCE_DIR}/src/database/entities/RouteRule.cpp
        LIBRARIES Qt6::Widgets Qt6::Network Throned::FkYAML Throned::SQLiteCpp)

throned_add_qt_test(throned_rule_line_tests
        SOURCES test_rule_line.cpp ${PROJECT_SOURCE_DIR}/src/database/entities/RuleLine.cpp
        LIBRARIES Qt6::Widgets Qt6::Network)

throned_add_qt_test(throned_share_link_tests
        SOURCES test_share_link.cpp ${PROJECT_SOURCE_DIR}/src/global/ShareLinkB64.cpp)

throned_add_qt_test(throned_outbound_tests
        TIMEOUT 30
        SOURCES
            test_outbounds.cpp
            ${PROJECT_SOURCE_DIR}/src/configs/outbounds/masque.cpp
            ${PROJECT_SOURCE_DIR}/src/configs/outbounds/naive.cpp
            ${PROJECT_SOURCE_DIR}/src/configs/outbounds/trusttunnel.cpp
            ${PROJECT_SOURCE_DIR}/src/configs/outbounds/http.cpp
            ${PROJECT_SOURCE_DIR}/src/configs/common/Outbound.cpp
            ${PROJECT_SOURCE_DIR}/src/configs/common/DialFields.cpp
            ${PROJECT_SOURCE_DIR}/src/configs/common/TLS.cpp
            ${PROJECT_SOURCE_DIR}/src/configs/common/QUICFields.cpp
            ${PROJECT_SOURCE_DIR}/src/configs/common/transport.cpp
            ${PROJECT_SOURCE_DIR}/src/configs/common/multiplex.cpp
            ${PROJECT_SOURCE_DIR}/src/configs/common/xrayStreamSetting.cpp
            ${PROJECT_SOURCE_DIR}/src/configs/common/xrayMultiplex.cpp
            ${PROJECT_SOURCE_DIR}/src/configs/common/utils.cpp
            ${PROJECT_SOURCE_DIR}/src/database/SettingsRepo.cpp
            ${PROJECT_SOURCE_DIR}/src/database/Database.cpp
            ${PROJECT_SOURCE_DIR}/src/global/Utils.cpp
            ${PROJECT_SOURCE_DIR}/src/global/ReleaseNotes.cpp
        LIBRARIES Qt6::Widgets Qt6::Network Qt6::Svg
            Throned::FkYAML Throned::Protocol Throned::SQLiteCpp
)
throned_test_needs_url_scheme(throned_outbound_tests)
if (WIN32)
    target_sources(throned_outbound_tests PRIVATE
        ${PROJECT_SOURCE_DIR}/src/sys/windows/guihelper.cpp
        ${PROJECT_SOURCE_DIR}/src/sys/windows/WinVersion.cpp)
    target_link_libraries(throned_outbound_tests PRIVATE ${PLATFORM_LIBRARIES})
endif ()

throned_add_qt_test(throned_sub_info_tests
        SOURCES test_sub_info.cpp ${PROJECT_SOURCE_DIR}/src/configs/sub/SubInfo.cpp)

throned_add_qt_test(throned_log_level_tests SOURCES test_log_level.cpp)

throned_add_qt_test(throned_release_notes_tests
        SOURCES test_release_notes.cpp ${PROJECT_SOURCE_DIR}/src/global/ReleaseNotes.cpp)

add_test(NAME throned_protocol_editors
    COMMAND Throned -ui-preview ${PROJECT_BINARY_DIR}/protocol-editors
        -ui-preview-protocols -theme "Throned Graphite")
set_tests_properties(throned_protocol_editors PROPERTIES TIMEOUT 30
    ENVIRONMENT "QT_FORCE_STDERR_LOGGING=1")
if (UNIX AND NOT APPLE)
    set_property(TEST throned_protocol_editors APPEND PROPERTY
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif ()
