cmake_minimum_required(VERSION 3.20)

project(Throned VERSION 0.1)

# Ninja learns header dependencies by matching the prefix of cl.exe's /showIncludes
# lines against the one CMake recorded here at configure time. A localised toolchain
# prints that prefix in the console codepage, so when the configure and the build run
# in different environments the match silently fails: ninja then rebuilds nothing when
# a header changes, and a header that alters a class layout leaves half the objects on
# the old one. That links, runs, and crashes on a wild pointer with no bad code in
# sight. CI never sees it because CI always builds from an empty directory.
#
# What matters is that both phases agree, not that the prefix is English: a toolchain
# without the English pack keeps printing its own language under VSLANG=1033, and that
# is fine as long as the configure saw the same thing. Deliberately only a warning,
# because setting VSLANG here would fix the configure and not the build - the same
# mismatch pointing the other way. It has to be in the environment of both, which is
# what CMakePresets.json does.
if (CMAKE_GENERATOR MATCHES "Ninja" AND MSVC AND NOT "$ENV{VSLANG}" STREQUAL "1033")
    message(WARNING
        "VSLANG is not 1033, so ninja may silently stop tracking header "
        "dependencies and produce a binary built from a mix of old and new headers. "
        "Configure and build with VSLANG=1033 in the environment (cmake --preset "
        "windows-ninja), or verify by touching include/ui/mainwindow.h and checking "
        "that a rebuild does far more than a handful of edges.")
endif()

set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    string(REPLACE "/Zi" "/Z7" CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO}")
    string(REPLACE "/Zi" "/Z7" CMAKE_C_FLAGS_RELWITHDEBINFO "${CMAKE_C_FLAGS_RELWITHDEBINFO}")
endif()

#### Dead code stripping ####
# link.exe disables /OPT:REF and /OPT:ICF whenever /DEBUG is passed.
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
# Qt headers are the bulk of every translation unit and are re-parsed by each one.
# Precompiling them is the cheapest large win, but it force-includes them everywhere,
# so like the unity build it hides a missing #include -- turn both off for the build
# that has to be honest about that.
option(THRONED_PRECOMPILE_HEADERS "Precompile the common Qt headers" ON)

# Uses whichever compiler cache is installed; absent, the build simply runs as before.
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

# Window chrome. Qt::FramelessWindowHint drops the whole native frame, and with it the
# open/close animations, Aero Snap, Snap Layout and the resize borders -- which is why
# the hand-rolled resizer never felt right. QWindowKit keeps the frame and replaces only
# what the caption draws, the way Chrome and VS Code do it. Pinned to a commit so a
# release never depends on whatever the branch looked like that day.
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

#### Platform Variables ####
if (APPLE)
    include("cmake/macos/macos.cmake")
elseif (WIN32)
    include("cmake/windows/windows.cmake")
else ()
    include("cmake/linux/linux.cmake")
endif ()

#### For Dumps ####
if (NOT MSVC)
    add_compile_options(-fno-omit-frame-pointer)
endif()

# Qt drops file/line from QMessageLogContext in non-debug builds without this.
add_compile_definitions(QT_MESSAGELOGCONTEXT)

#### default prefix path ####

message("[CMAKE_PREFIX_PATH] ${CMAKE_PREFIX_PATH}")

# for some cross toolchain
list(APPEND CMAKE_FIND_ROOT_PATH ${CMAKE_PREFIX_PATH})
message("[CMAKE_FIND_ROOT_PATH] ${CMAKE_FIND_ROOT_PATH}")

# Must precede every add_subdirectory/add_library below.
set(BUILD_SHARED_LIBS OFF)

include("cmake/print.cmake")
include("cmake/QHotkey.cmake")

find_package(Threads)

if (NKR_PACKAGE OR NKR_PACKAGE_MACOS)
    nkr_add_compile_definitions(NKR_CPP_USE_APPDATA)
endif ()

include("cmake/myproto.cmake")
list(APPEND NKR_EXTERNAL_TARGETS myproto)

list(APPEND NKR_EXTERNAL_TARGETS qhotkey)

# Sources
set(PROJECT_SOURCES
        ${PLATFORM_SOURCES}

        src/main.cpp
        src/global/Configs.cpp
        include/global/ReleaseNotes.hpp
        src/global/ReleaseNotes.cpp
        src/global/Utils.cpp
        include/global/ShareLinkB64.hpp
        src/global/ShareLinkB64.cpp
        include/global/Logger.hpp
        src/global/Logger.cpp
        include/global/PeriodicRunner.hpp
        src/global/PeriodicRunner.cpp
        src/global/HTTPRequestHelper.cpp
        src/global/DeviceDetailsHelper.cpp
        src/global/CountryHelper.cpp
        include/global/LocalNetwork.hpp
        src/global/LocalNetwork.cpp
        include/global/OTP.hpp
        src/global/OTP.cpp
        include/global/OtpPlaceholder.hpp
        src/global/OtpPlaceholder.cpp
        include/global/VpnCredentialOverride.hpp
        src/global/VpnCredentialOverride.cpp

        3rdparty/qrcodegen.cpp
        3rdparty/QtExtKeySequenceEdit.cpp
        3rdparty/QrDecoder.cpp

        3rdparty/qv2ray/v2/ui/LogHighlighter.cpp
        3rdparty/qv2ray/v2/ui/QvAutoCompleteTextEdit.cpp
        3rdparty/qv2ray/v2/proxy/QvProxyConfigurator.cpp

        3rdparty/quirc/decode.c
        3rdparty/quirc/identify.c
        3rdparty/quirc/quirc.c
        3rdparty/quirc/version_db.c

        3rdparty/SQLiteCpp/src/Backup.cpp
        3rdparty/SQLiteCpp/src/Column.cpp
        3rdparty/SQLiteCpp/src/Database.cpp
        3rdparty/SQLiteCpp/src/Exception.cpp
        3rdparty/SQLiteCpp/src/Savepoint.cpp
        3rdparty/SQLiteCpp/src/sqlite3.c
        3rdparty/SQLiteCpp/src/Statement.cpp
        3rdparty/SQLiteCpp/src/Transaction.cpp

        src/api/RPC.cpp

        src/stats/traffic/TrafficLooper.cpp
        include/stats/traffic/TrafficStatsManager.hpp
        src/stats/traffic/TrafficStatsManager.cpp

        include/configs/sub/GroupUpdater.hpp
        src/configs/sub/GroupUpdater.cpp
        include/configs/sub/SubInfo.h
        src/configs/sub/SubInfo.cpp
        include/configs/sub/SubscriptionScan.hpp
        include/configs/sub/SubscriptionParser.hpp
        src/configs/sub/SubscriptionParser.cpp
        include/configs/sub/SubscriptionReconcile.hpp
        src/configs/sub/SubscriptionReconcile.cpp
        include/configs/sub/RouteUpdater.hpp
        src/configs/sub/RouteUpdater.cpp
        src/configs/sub/clash.cpp
        include/configs/sub/vpnFileImport.hpp
        src/configs/sub/vpnFileImport.cpp

        include/sys/Process.hpp
        src/sys/Process.cpp

        include/sys/ProcessMetrics.hpp
        src/sys/ProcessMetrics.cpp

        include/sys/UrlScheme.hpp
        src/sys/UrlScheme.cpp

        include/ui/setting/ThemeManager.hpp
        src/ui/setting/ThemeManager.cpp
        src/ui/setting/Icon.cpp
        include/ui/widget/MaterialIcon.h
        src/ui/widget/MaterialIcon.cpp
        include/ui/widget/ThronedTitleBar.h
        src/ui/widget/ThronedTitleBar.cpp
        src/ui/widget/ThronedWindowChrome.cpp
        include/ui/widget/ThronedToggle.h
        src/ui/widget/ThronedToggle.cpp
        include/ui/widget/UpdateStatusWidget.h
        src/ui/widget/UpdateStatusWidget.cpp

        src/ui/mainWindow/mainwindow_setup.cpp
        include/ui/mainWindow/QuickAddOverlay.h
        src/ui/mainWindow/QuickAddOverlay.cpp
        src/ui/mainWindow/mainwindow_events.cpp
        src/ui/mainWindow/mainwindow_groups.cpp
        src/ui/mainWindow/mainwindow_view.cpp
        src/ui/mainWindow/mainwindow_connections.cpp
        src/ui/mainWindow/mainwindow_profiles.cpp
        src/ui/mainWindow/mainwindow_deeplink.cpp
        src/ui/mainWindow/mainwindow_system.cpp
        src/ui/mainWindow/mainwindow_log.cpp
        src/ui/mainWindow/mainwindow_hotkeys.cpp
        src/ui/mainWindow/mainwindow_profile_lifecycle.cpp
        src/ui/mainWindow/mainwindow_autoselector.cpp
        src/ui/mainWindow/TestRunner.cpp
        include/ui/mainWindow/MainWindowInternal.h
        include/ui/mainWindow/TestRunner.h
        include/ui/mainwindow.h
        include/ui/mainwindow.ui
        include/ui/widget/ActionButton.h
        src/ui/widget/ActionButton.cpp
        include/ui/widget/StartStopButton.hpp
        src/ui/widget/StartStopButton.cpp
        include/ui/widget/ThroughputChart.h
        src/ui/widget/ThroughputChart.cpp
        include/ui/widget/GroupTabBar.h
        include/ui/widget/SubscriptionPopover.hpp
        src/ui/widget/SubscriptionPopover.cpp
        src/ui/widget/GroupTabBar.cpp
        include/ui/widget/TrayPopupFrame.hpp
        src/ui/widget/TrayPopupFrame.cpp
        include/ui/widget/TrayProfileSelector.hpp
        src/ui/widget/TrayProfileSelector.cpp
        include/control/ThronedControl.h
        src/control/ThronedControl.cpp
        include/ui/widget/RoutingQuickMenu.hpp
        src/ui/widget/RoutingQuickMenu.cpp
        include/ui/widget/TrayOtpCodes.hpp
        src/ui/widget/TrayOtpCodes.cpp
        include/ui/widget/json/JsonTree.h
        src/ui/widget/json/JsonTree.cpp
        include/ui/widget/json/JsonValidator.h
        include/ui/widget/json/JsonSchemaValidator.h
        src/ui/widget/json/JsonSchemaValidator.cpp
        include/ui/widget/json/SchemaStore.h
        src/ui/widget/json/SchemaStore.cpp
        include/ui/widget/json/JsonCodeEdit.h
        src/ui/widget/json/JsonCodeEdit.cpp
        include/ui/widget/json/JsonIssueList.h
        src/ui/widget/json/JsonIssueList.cpp
        include/ui/widget/json/JsonEditorDialog.h
        src/ui/widget/json/JsonEditorDialog.cpp
        include/ui/utils/DataViewHtmlGenerator.h
        src/ui/utils/DataViewHtmlGenerator.cpp
        include/ui/utils/ProfilesTableModel.h
        include/ui/utils/ProfileRowDelegate.h
        src/ui/utils/ProfileRowDelegate.cpp
        src/ui/utils/ProfilesTableModel.cpp
        include/ui/utils/ProfilesFilterProxyModel.h
        src/ui/utils/ProfilesFilterProxyModel.cpp
        include/ui/utils/ProfilesTableView.h
        src/ui/utils/ProfilesTableView.cpp
        include/ui/utils/ProfilesTableVerticalHeader.h
        src/ui/utils/ProfilesTableVerticalHeader.cpp
        include/ui/utils/ProfilesTableFilterHeader.h
        include/ui/utils/ConnectionsFilterHeader.h
        include/ui/utils/ConnectionsTableModel.h
        src/ui/utils/ConnectionsTableModel.cpp
        include/ui/utils/ConnectionsFilterProxyModel.h
        src/ui/utils/ConnectionsFilterProxyModel.cpp
        include/ui/utils/ConnectionCloseDelegate.h
        src/ui/utils/ConnectionCloseDelegate.cpp
        include/ui/utils/ScreenQrScanner.h
        src/ui/utils/ScreenQrScanner.cpp

        include/ui/profile/dialog_edit_profile.h
        src/ui/profile/dialog_edit_profile.cpp
        src/ui/profile/dialog_edit_profile_xhttp.cpp
        include/ui/profile/dialog_edit_profile.ui
        include/ui/group/dialog_edit_group.h
        src/ui/group/dialog_edit_group.cpp
        include/ui/group/dialog_edit_group.ui

        include/ui/profile/edit_chain.h
        src/ui/profile/edit_chain.cpp
        include/ui/profile/edit_chain.ui
        include/ui/profile/edit_autoselector.h
        src/ui/profile/edit_autoselector.cpp
        include/ui/profile/edit_autoselector.ui
        include/ui/profile/edit_shadowsocks.h
        src/ui/profile/edit_shadowsocks.cpp
        include/ui/profile/edit_shadowsocks.ui
        include/ui/profile/edit_vmess.h
        src/ui/profile/edit_vmess.cpp
        include/ui/profile/edit_vmess.ui
        include/ui/profile/edit_vless.h
        src/ui/profile/edit_vless.cpp
        include/ui/profile/edit_vless.ui
        include/ui/profile/edit_anytls.h
        src/ui/profile/edit_anytls.cpp
        include/ui/profile/edit_anytls.ui
        include/ui/profile/edit_mieru.h
        src/ui/profile/edit_mieru.cpp
        include/ui/profile/edit_mieru.ui
        include/ui/profile/edit_snell.h
        src/ui/profile/edit_snell.cpp
        include/ui/profile/edit_snell.ui
        include/ui/profile/edit_tailscale.h
        src/ui/profile/edit_tailscale.cpp
        include/ui/profile/edit_tailscale.ui
        include/ui/profile/edit_trojan.h
        src/ui/profile/edit_trojan.cpp
        include/ui/profile/edit_trojan.ui
        include/ui/profile/edit_http.h
        src/ui/profile/edit_http.cpp
        include/ui/profile/edit_http.ui
        include/ui/profile/edit_socks.h
        src/ui/profile/edit_socks.cpp
        include/ui/profile/edit_socks.ui
        include/ui/profile/edit_hysteria.h
        src/ui/profile/edit_hysteria.cpp
        include/ui/profile/edit_hysteria.ui
        include/ui/profile/edit_hysteria_realm.h
        src/ui/profile/edit_hysteria_realm.cpp
        include/ui/profile/edit_hysteria_realm.ui
        include/ui/profile/edit_tuic.h
        src/ui/profile/edit_tuic.cpp
        include/ui/profile/edit_tuic.ui
        include/ui/profile/edit_juicity.h
        src/ui/profile/edit_juicity.cpp
        include/ui/profile/edit_juicity.ui
        include/ui/profile/edit_trusttunnel.h
        src/ui/profile/edit_trusttunnel.cpp
        include/ui/profile/edit_trusttunnel.ui
        include/ui/profile/edit_shadowtls.h
        src/ui/profile/edit_shadowtls.cpp
        include/ui/profile/edit_shadowtls.ui
        include/ui/profile/edit_naive.h
        src/ui/profile/edit_naive.cpp
        include/ui/profile/edit_naive.ui

        include/ui/profile/edit_custom.h
        src/ui/profile/edit_custom.cpp
        include/ui/profile/edit_custom.ui

        include/ui/profile/edit_extra_core.h
        src/ui/profile/edit_extra_core.cpp
        include/ui/profile/edit_extra_core.ui

        include/ui/profile/edit_wireguard.h
        src/ui/profile/edit_wireguard.cpp
        include/ui/profile/edit_wireguard.ui

        include/ui/profile/edit_wireguard_amnezia.h
        src/ui/profile/edit_wireguard_amnezia.cpp
        include/ui/profile/edit_wireguard_amnezia.ui

        include/ui/profile/editor_table_utils.h

        include/ui/profile/edit_openvpn.h
        src/ui/profile/edit_openvpn.cpp
        include/ui/profile/edit_openvpn.ui

        include/ui/profile/edit_openvpn_advanced.h
        src/ui/profile/edit_openvpn_advanced.cpp
        include/ui/profile/edit_openvpn_advanced.ui

        include/ui/profile/edit_openconnect.h
        src/ui/profile/edit_openconnect.cpp
        include/ui/profile/edit_openconnect.ui

        include/ui/profile/edit_openconnect_advanced.h
        src/ui/profile/edit_openconnect_advanced.cpp
        include/ui/profile/edit_openconnect_advanced.ui

        include/ui/profile/dialog_vpn_auth.h
        src/ui/profile/dialog_vpn_auth.cpp
        include/ui/profile/dialog_vpn_auth.ui

        src/ui/setting/dialog_basic_settings.cpp
        include/ui/setting/dialog_basic_settings.h
        include/ui/setting/dialog_basic_settings.ui

        src/ui/group/dialog_manage_groups.cpp
        include/ui/group/dialog_manage_groups.h
        include/ui/group/dialog_manage_groups.ui

        src/ui/setting/dialog_manage_routes.cpp
        include/ui/setting/dialog_manage_routes.h
        include/ui/setting/dialog_manage_routes.ui

        src/ui/setting/dialog_vpn_settings.cpp
        include/ui/setting/dialog_vpn_settings.h
        include/ui/setting/dialog_vpn_settings.ui

        src/ui/setting/dialog_preset_settings.cpp
        include/ui/setting/dialog_preset_settings.h
        include/ui/setting/dialog_preset_settings.ui

        src/ui/setting/dialog_dpi_bypass.cpp
        include/ui/setting/dialog_dpi_bypass.h
        include/ui/setting/dialog_dpi_bypass.ui

        src/ui/setting/dialog_otp_manager.cpp
        include/ui/setting/dialog_otp_manager.h
        include/ui/setting/dialog_otp_manager.ui

        src/ui/setting/dialog_edit_otp.cpp
        include/ui/setting/dialog_edit_otp.h
        include/ui/setting/dialog_edit_otp.ui

        src/ui/setting/OtpItem.cpp
        include/ui/setting/OtpItem.h
        include/ui/setting/OtpItem.ui
        src/ui/setting/OtpListWidget.cpp
        include/ui/setting/OtpListWidget.h

        src/ui/stats/dialog_traffic_stats.cpp
        include/ui/stats/dialog_traffic_stats.h
        include/ui/stats/dialog_traffic_stats.ui
        src/ui/stats/TrafficChartWidget.cpp
        include/ui/stats/TrafficChartWidget.h

        src/ui/stats/dialog_runtime_stats.cpp
        include/ui/stats/dialog_runtime_stats.h
        include/ui/stats/dialog_runtime_stats.ui
        src/ui/stats/dialog_endpoint_details.cpp
        include/ui/stats/dialog_endpoint_details.h
        src/ui/stats/dialog_site_reachability.cpp
        include/ui/stats/dialog_site_reachability.h
        include/ui/stats/dialog_endpoint_details.ui
        src/ui/stats/MiniChartWidget.cpp
        include/ui/stats/MiniChartWidget.h

        src/ui/setting/dialog_hotkey.cpp
        include/ui/setting/dialog_hotkey.h
        include/ui/setting/dialog_hotkey.ui

        src/ui/profile/ProxyItem.cpp
        include/ui/profile/ProxyItem.h
        include/ui/profile/ProxyItem.ui
        src/ui/group/GroupItem.cpp
        include/ui/group/GroupItem.h
        include/ui/group/GroupItem.ui
        src/ui/setting/RouteItem.cpp
        include/ui/setting/RouteItem.h
        include/ui/setting/RouteItem.ui
        include/ui/setting/ApplicationPickerDialog.h
        src/ui/setting/ApplicationPickerDialog.cpp
        include/ui/setting/RouteProfileSimpleEditor.h
        src/ui/setting/RouteProfileSimpleEditor.cpp

        src/ui/setting/RawRouteItem.cpp
        include/ui/setting/RawRouteItem.h

        res/Throned.qrc
        ${QV2RAY_RC}
        res/darkstyle.qrc
        res/theme/feiyangqingyun/qss.qrc
        src/ui/profile/edit_ssh.cpp
        include/ui/profile/edit_ssh.h
        include/ui/profile/edit_ssh.ui
        include/ui/mainwindow_interface.h
        include/stats/connections/connectionLister.hpp
        src/stats/connectionLister/connectionLister.cpp
        include/sys/windows/eventHandler.h
        include/configs/baseConfig.h
        include/configs/common/DialFields.h
        include/configs/common/QUICFields.h
        include/configs/common/TLS.h
        include/configs/common/multiplex.h
        include/configs/common/transport.h
        include/configs/common/Outbound.h
        include/configs/common/OutboundFactory.h

        # common impl
        src/configs/common/DialFields.cpp
        src/configs/common/QUICFields.cpp
        src/configs/common/TLS.cpp
        src/configs/common/multiplex.cpp
        src/configs/common/transport.cpp
        src/configs/common/Outbound.cpp
        src/configs/common/OutboundFactory.cpp
        include/configs/outbounds/socks.h
        include/configs/outbounds/http.h
        include/configs/outbounds/shadowsocks.h
        include/configs/outbounds/vmess.h
        include/configs/outbounds/trojan.h
        include/configs/outbounds/hysteria.h
        include/configs/outbounds/vless.h
        include/configs/outbounds/tuic.h
        include/configs/outbounds/juicity.h
        include/configs/outbounds/trusttunnel.h
        include/configs/outbounds/naive.h
        include/configs/outbounds/shadowtls.h
        include/configs/outbounds/anyTLS.h
        include/configs/outbounds/mieru.h
        include/configs/outbounds/snell.h
        include/configs/outbounds/ssh.h
        
        # outbounds impl
        src/configs/outbounds/anyTLS.cpp
        src/configs/outbounds/mieru.cpp
        src/configs/outbounds/snell.cpp
        src/configs/outbounds/http.cpp
        src/configs/outbounds/hysteria.cpp
        src/configs/outbounds/shadowsocks.cpp
        src/configs/outbounds/socks.cpp
        src/configs/outbounds/ssh.cpp
        src/configs/outbounds/tailscale.cpp
        src/configs/outbounds/trojan.cpp
        src/configs/outbounds/tuic.cpp
        src/configs/outbounds/juicity.cpp
        src/configs/outbounds/trusttunnel.cpp
        src/configs/outbounds/naive.cpp
        src/configs/outbounds/shadowtls.cpp
        src/configs/outbounds/vless.cpp
        src/configs/outbounds/vmess.cpp
        src/configs/outbounds/wireguard.cpp
        include/configs/outbounds/openvpn.h
        src/configs/outbounds/openvpn.cpp
        include/configs/outbounds/openconnect.h
        src/configs/outbounds/openconnect.cpp
        src/configs/outbounds/custom.cpp
        include/configs/generate.h
        src/configs/generate.cpp
        include/configs/validate.h
        src/configs/validate.cpp
        include/configs/common/utils.h
        src/configs/common/utils.cpp
        include/configs/outbounds/custom.h
        include/configs/outbounds/chain.h
        include/configs/outbounds/autoselector.h
        src/configs/outbounds/autoselector.cpp
        include/configs/AutoSelectorPlan.h
        src/configs/AutoSelectorPlan.cpp
        include/stats/autoselector/AutoSelectorMonitor.hpp
        src/stats/autoselector/AutoSelectorMonitor.cpp
        include/ui/stats/dialog_auto_selector.h
        src/ui/stats/dialog_auto_selector.cpp
        include/configs/outbounds/extracore.h
        include/ui/profile/edit_advanced.h
        src/ui/profile/edit_advanced.cpp
        include/ui/profile/edit_advanced.ui
        include/configs/common/xrayStreamSetting.h
        include/configs/outbounds/xrayVless.h
        include/configs/common/xrayMultiplex.h
        src/configs/common/xrayMultiplex.cpp
        src/configs/common/xrayStreamSetting.cpp
        src/configs/outbounds/xrayVless.cpp
        include/ui/profile/edit_xrayvless.h
        src/ui/profile/edit_xrayvless.cpp
        include/ui/profile/edit_xrayvless.ui
        include/ui/profile/edit_direct.h
        include/ui/profile/edit_direct.ui
        src/ui/profile/edit_direct.cpp

        include/database/Database.h
        src/database/Database.cpp
        include/database/ProfilesRepo.h
        include/database/GroupsRepo.h
        include/database/RoutesRepo.h
        include/database/OtpProfilesRepo.h
        include/database/SettingsRepo.h
        include/database/TrafficStatsRepo.h

        include/database/entities/Profile.h
        include/database/entities/Group.h
        include/database/entities/RouteProfile.h
        include/database/entities/RouteRule.h
        include/database/entities/OtpProfile.h

        src/database/entities/Profile.cpp
        src/database/entities/Group.cpp
        src/database/entities/RouteProfile.cpp
        src/database/entities/RuleLine.cpp
        src/database/entities/RouteRule.cpp
        src/database/entities/OtpProfile.cpp

        src/database/ProfilesRepo.cpp
        src/database/GroupsRepo.cpp
        src/database/RoutesRepo.cpp
        src/database/OtpProfilesRepo.cpp
        src/database/TrafficStatsRepo.cpp

        include/database/DatabaseManager.h
        src/database/DatabaseManager.cpp

        include/database/SettingsRepo.h
        src/database/SettingsRepo.cpp
        include/configs/sub/warp.h
        src/configs/sub/warp.cpp
        include/global/Common.h
        src/global/Common.cpp
)

if (NOT APPLE AND Qt6_VERSION VERSION_GREATER_EQUAL 6.9.0)
    list(APPEND PROJECT_SOURCES res/EmojiFont.qrc)
    if (WIN32)
        list(APPEND PROJECT_SOURCES res/EmojiFontWin.qrc)
    endif ()
    # rcc's default threshold leaves these TTFs uncompressed.
    set_property(SOURCE res/EmojiFont.qrc res/EmojiFontWin.qrc
            PROPERTY AUTORCC_OPTIONS "--compress-algo;zlib;--compress;9;--threshold;5")
endif ()

file(GLOB_RECURSE DASHBOARD_FILES RELATIVE ${CMAKE_SOURCE_DIR}/res/dashboard ${CMAKE_SOURCE_DIR}/res/dashboard/*)
# Bracket syntax: in a quoted argument "\\." reaches the regex as ".", matching everything.
list(FILTER DASHBOARD_FILES EXCLUDE REGEX [[(^|/)\.]])
if (DASHBOARD_FILES)
    set(DASHBOARD_QRC_ENTRIES "")
    foreach (DASHBOARD_FILE ${DASHBOARD_FILES})
        string(APPEND DASHBOARD_QRC_ENTRIES
                "        <file alias=\"${DASHBOARD_FILE}\">${CMAKE_SOURCE_DIR}/res/dashboard/${DASHBOARD_FILE}</file>\n")
    endforeach ()
    file(WRITE ${CMAKE_BINARY_DIR}/dashboard.qrc
            "<RCC>\n    <qresource prefix=\"/dashboard\">\n${DASHBOARD_QRC_ENTRIES}    </qresource>\n</RCC>\n")
    list(APPEND PROJECT_SOURCES ${CMAKE_BINARY_DIR}/dashboard.qrc)
    set_property(SOURCE ${CMAKE_BINARY_DIR}/dashboard.qrc
            PROPERTY AUTORCC_OPTIONS "--compress-algo;zlib;--compress;9;--threshold;5")
endif ()

# Qt exe
qt_add_executable(Throned
        MANUAL_FINALIZATION
        ${PROJECT_SOURCES}
)

# Target

# MSVC defaults to /W1, which hid a dead Qt5-era comparison and two unchecked
# QFile::open calls. C4996 stays off: the replacements Qt suggests need 6.7+,
# and the system-Qt packages build against the 6.2 that Ubuntu ships.
if (MSVC)
    target_compile_options(Throned PRIVATE /W3 /wd4996)
    set_source_files_properties(
            3rdparty/quirc/decode.c
            3rdparty/quirc/identify.c
            3rdparty/quirc/quirc.c
            3rdparty/quirc/version_db.c
            3rdparty/SQLiteCpp/src/sqlite3.c
            PROPERTIES COMPILE_OPTIONS /W0)
endif ()

set_property(TARGET Throned PROPERTY AUTOUIC ON)
set_property(TARGET Throned PROPERTY AUTOMOC ON)
set_property(TARGET Throned PROPERTY AUTORCC ON)


# Target Source macOS

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
        # Unity builds share includes between neighbouring sources, which hides a
        # missing one until a different grouping (another platform, another day)
        # splits them apart. Turn this off to compile each file on its own and
        # find those before CI does.
        UNITY_BUILD ${THRONED_UNITY_BUILD}
)

set_property(SOURCE "3rdparty/SQLiteCpp/src/sqlite3.c" PROPERTY SKIP_UNITY_BUILD_INCLUSION ON)

# Verified to link. OMIT_VIRTUALTABLE/VIEW/ANALYZE/CTE/WINDOWFUNC/DECLTYPE all break this build.
target_compile_definitions(Throned PRIVATE
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

# Target Source Translations

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

# Target Link

target_link_libraries(Throned PRIVATE
        Qt6::Widgets Qt6::Network Qt6::Svg ${QT_DBUS_LIBRARY}
        QWindowKit::Widgets
        Threads::Threads
        ${NKR_EXTERNAL_TARGETS}
        ${PLATFORM_LIBRARIES}
)

if (THRONED_PRECOMPILE_HEADERS)
    # Restricted to C++: the target also carries C sources, and CMake would otherwise
    # try to precompile these as C and fail on the first Qt header.
    target_precompile_headers(Throned PRIVATE
            "$<$<COMPILE_LANGUAGE:CXX>:${CMAKE_CURRENT_SOURCE_DIR}/include/global/PrecompiledHeaders.hpp>")
endif ()

qt_finalize_executable(Throned)

# Tests
# Only the dependency-free pieces are covered here: the checker links against
# QtCore alone, so `ctest` stays runnable without a display or a built app.
if (Qt6Test_FOUND)
    enable_testing()
    qt_add_executable(throned_site_reachability_tests
            tests/test_site_reachability.cpp
            src/ui/stats/dialog_site_reachability.cpp
            include/ui/stats/dialog_site_reachability.h
            include/ui/setting/ThemeManager.hpp
    )
    set_target_properties(throned_site_reachability_tests PROPERTIES UNITY_BUILD OFF AUTOMOC ON)
    target_link_libraries(throned_site_reachability_tests PRIVATE Qt6::Test Qt6::Widgets Qt6::Network myproto)
    qt_import_plugins(throned_site_reachability_tests INCLUDE Qt6::QOffscreenIntegrationPlugin)
    add_test(NAME throned_site_reachability_tests COMMAND throned_site_reachability_tests -platform offscreen)
    set_tests_properties(throned_site_reachability_tests PROPERTIES TIMEOUT 30)

    qt_add_executable(throned_tests
            tests/test_validate.cpp
            src/configs/validate.cpp
    )
    set_target_properties(throned_tests PROPERTIES UNITY_BUILD OFF AUTOMOC ON)
    target_link_libraries(throned_tests PRIVATE Qt6::Test)
    add_test(NAME throned_tests COMMAND throned_tests)

    # Rule serialisation needs the entity, which drags in Qt Widgets/Network headers,
    # so it gets its own binary rather than widening the checker above.
    qt_add_executable(throned_route_tests
            tests/test_route_rule.cpp
            src/database/entities/RouteRule.cpp
    )
    set_target_properties(throned_route_tests PROPERTIES UNITY_BUILD OFF AUTOMOC ON)
    target_link_libraries(throned_route_tests PRIVATE Qt6::Test Qt6::Widgets Qt6::Network)
    add_test(NAME throned_route_tests COMMAND throned_route_tests)

    qt_add_executable(throned_rule_line_tests
            tests/test_rule_line.cpp
            src/database/entities/RuleLine.cpp
    )
    set_target_properties(throned_rule_line_tests PROPERTIES UNITY_BUILD OFF AUTOMOC ON)
    target_link_libraries(throned_rule_line_tests PRIVATE Qt6::Test Qt6::Widgets Qt6::Network)
    add_test(NAME throned_rule_line_tests COMMAND throned_rule_line_tests)

    qt_add_executable(throned_share_link_tests
            tests/test_share_link.cpp
            src/global/ShareLinkB64.cpp
    )
    set_target_properties(throned_share_link_tests PROPERTIES UNITY_BUILD OFF AUTOMOC ON)
    target_link_libraries(throned_share_link_tests PRIVATE Qt6::Test)
    add_test(NAME throned_share_link_tests COMMAND throned_share_link_tests)

    qt_add_executable(throned_sub_info_tests
            tests/test_sub_info.cpp
            src/configs/sub/SubInfo.cpp
    )
    set_target_properties(throned_sub_info_tests PROPERTIES UNITY_BUILD OFF AUTOMOC ON)
    target_link_libraries(throned_sub_info_tests PRIVATE Qt6::Test)
    add_test(NAME throned_sub_info_tests COMMAND throned_sub_info_tests)

    qt_add_executable(throned_log_level_tests tests/test_log_level.cpp)
    set_target_properties(throned_log_level_tests PROPERTIES UNITY_BUILD OFF AUTOMOC ON)
    target_link_libraries(throned_log_level_tests PRIVATE Qt6::Test)
    add_test(NAME throned_log_level_tests COMMAND throned_log_level_tests)

    qt_add_executable(throned_release_notes_tests
            tests/test_release_notes.cpp
            src/global/ReleaseNotes.cpp
    )
    set_target_properties(throned_release_notes_tests PROPERTIES UNITY_BUILD OFF AUTOMOC ON)
    target_link_libraries(throned_release_notes_tests PRIVATE Qt6::Test)
    add_test(NAME throned_release_notes_tests COMMAND throned_release_notes_tests)
endif ()
