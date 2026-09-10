cmake_minimum_required(VERSION 3.21)

if (NOT DEFINED THRONED_EXECUTABLE OR NOT EXISTS "${THRONED_EXECUTABLE}")
    message(FATAL_ERROR "Pass the built application with -DTHRONED_EXECUTABLE=/path/to/Throned")
endif ()

get_filename_component(THRONED_EXECUTABLE "${THRONED_EXECUTABLE}" ABSOLUTE)
get_filename_component(_build_dir "${THRONED_EXECUTABLE}" DIRECTORY)
if (DEFINED QT_RUNTIME_DIR)
    get_filename_component(QT_RUNTIME_DIR "${QT_RUNTIME_DIR}" ABSOLUTE)
    if (WIN32)
        set(ENV{PATH} "${QT_RUNTIME_DIR};$ENV{PATH}")
    else ()
        set(ENV{PATH} "${QT_RUNTIME_DIR}:$ENV{PATH}")
    endif ()
    set(ENV{QT_PLUGIN_PATH} "${QT_RUNTIME_DIR}/../plugins")
endif ()
if (NOT DEFINED OUTPUT_DIR)
    set(OUTPUT_DIR "${CMAKE_CURRENT_LIST_DIR}/../out/ui-scenarios")
endif ()
get_filename_component(OUTPUT_DIR "${OUTPUT_DIR}" ABSOLUTE)

string(TOLOWER "${CMAKE_HOST_SYSTEM_NAME}" _platform)
if (NOT DEFINED BASELINE_DIR)
    set(BASELINE_DIR "${CMAKE_CURRENT_LIST_DIR}/../tests/ui/baselines/${_platform}")
endif ()
get_filename_component(BASELINE_DIR "${BASELINE_DIR}" ABSOLUTE)

if (NOT DEFINED SNAPSHOT_COMPARE)
    set(_compare_names throned_snapshot_compare throned_snapshot_compare.exe)
    foreach (_name IN LISTS _compare_names)
        if (EXISTS "${_build_dir}/${_name}")
            set(SNAPSHOT_COMPARE "${_build_dir}/${_name}")
        endif ()
    endforeach ()
endif ()

if (NOT DEFINED SCENARIOS)
    set(SCENARIOS smoke)
endif ()
if (NOT DEFINED TIMEOUT_SECONDS)
    set(TIMEOUT_SECONDS 45)
endif ()
if (NOT DEFINED CHANNEL_TOLERANCE)
    set(CHANNEL_TOLERANCE 2)
endif ()
if (NOT DEFINED MAX_DIFFERENT_RATIO)
    set(MAX_DIFFERENT_RATIO 0.00005)
endif ()
# Gallery runs render for a human to look at; only a pinned environment may compare.
if (NOT DEFINED COMPARE_BASELINES)
    set(COMPARE_BASELINES ON)
endif ()

function(add_ui_scenario NAME)
    set(multi_value_args ARGS EXPECTED GROUPS)
    cmake_parse_arguments(PARSE_ARGV 1 SC "" "" "${multi_value_args}")
    set_property(GLOBAL APPEND PROPERTY UI_SCENARIO_NAMES "${NAME}")
    set_property(GLOBAL PROPERTY "UI_${NAME}_ARGS" "${SC_ARGS}")
    set_property(GLOBAL PROPERTY "UI_${NAME}_EXPECTED" "${SC_EXPECTED}")
    set_property(GLOBAL PROPERTY "UI_${NAME}_GROUPS" "${SC_GROUPS}")
endfunction()

add_ui_scenario(main-shell GROUPS all
    ARGS -ui-preview @PREFIX@ -ui-preview-docs -theme "Throned Graphite"
    EXPECTED
        main-shell-panel-opening.png main-shell-panel-closing.png
        main-shell-search-filtered.png main-shell-closed.png main-shell-logs.png
        main-shell-logs-menu.png main-shell-logs-menu-in-place.png main-shell-graph.png
        main-shell-window.png main-shell-menu.png main-shell-menu-in-place.png
        main-shell-stop-button.png main-shell-update-downloading.png
        main-shell-update-preparing.png main-shell-update-ready.png main-shell-update-error.png
        main-shell-restart-needed.png)
add_ui_scenario(quick-add GROUPS smoke all
    ARGS -ui-preview @PREFIX@ -ui-preview-docs -theme "Throned Graphite" -ui-preview-quick-add
    EXPECTED quick-add-quick-add.png quick-add-quick-add-detected.png
        quick-add-quick-add-manual-profile.png quick-add-quick-add-manual-group.png)
add_ui_scenario(quick-add-empty GROUPS all
    ARGS -ui-preview @PREFIX@ -ui-preview-docs -theme "Throned Graphite" -ui-preview-quick-add -ui-preview-empty
    EXPECTED quick-add-empty-empty-group.png quick-add-empty-quick-add.png
        quick-add-empty-quick-add-detected.png quick-add-empty-quick-add-manual-profile.png
        quick-add-empty-quick-add-manual-group.png)
add_ui_scenario(runtime-stats GROUPS all
    ARGS -ui-preview @PREFIX@ -ui-preview-docs -theme "Throned Graphite" -ui-preview-runtime-stats
    EXPECTED runtime-stats-runtime-stats.png)
add_ui_scenario(runtime-stats-short GROUPS all
    ARGS -ui-preview @PREFIX@ -ui-preview-docs -theme "Throned Graphite"
        -ui-preview-runtime-stats -ui-preview-panel-short
    EXPECTED runtime-stats-short-runtime-stats.png runtime-stats-short-runtime-bottom.png)
add_ui_scenario(graph-panel GROUPS all
    ARGS -ui-preview @PREFIX@ -ui-preview-docs -theme "Throned Graphite" -ui-preview-graph
    EXPECTED graph-panel-graph.png graph-panel-graph-controls.png graph-panel-graph-targets.png)
add_ui_scenario(graph-panel-short GROUPS all
    ARGS -ui-preview @PREFIX@ -ui-preview-docs -theme "Throned Graphite" -ui-preview-graph -ui-preview-panel-short
    EXPECTED graph-panel-short-graph.png graph-panel-short-graph-bottom.png)
add_ui_scenario(runtime-stats-compact GROUPS all
    ARGS -ui-preview @PREFIX@ -ui-preview-docs -theme "Throned Graphite"
        -ui-preview-runtime-stats -ui-preview-size 960x780 -lang en
    EXPECTED runtime-stats-compact-runtime-stats.png)
add_ui_scenario(runtime-stats-endpoints GROUPS all
    ARGS -ui-preview @PREFIX@ -ui-preview-docs -theme "Throned Graphite"
        -ui-preview-runtime-stats -ui-preview-runtime-endpoints -ui-preview-runtime-light
    EXPECTED runtime-stats-endpoints-runtime-stats.png runtime-stats-endpoints-runtime-endpoints.png)
add_ui_scenario(selection GROUPS smoke all
    ARGS -ui-preview @PREFIX@ -ui-preview-docs -theme "Throned Graphite" -ui-preview-selection
    EXPECTED selection-selected.png selection-cleared.png)
add_ui_scenario(settings GROUPS smoke all
    ARGS -ui-preview @PREFIX@ -ui-preview-docs -theme "Throned Graphite" -ui-preview-settings
    EXPECTED settings-settings.png)
add_ui_scenario(diagnostics GROUPS all
    ARGS -ui-preview @PREFIX@ -ui-preview-docs -theme "Throned Graphite" -ui-preview-diagnostics
    EXPECTED diagnostics-diagnostics-entry.png diagnostics-diagnostics-overview.png
        diagnostics-diagnostics-site.png diagnostics-diagnostics-app.png
        diagnostics-diagnostics-app-rule.png diagnostics-diagnostics-stats.png
        diagnostics-diagnostics-stats-proxyonly.png diagnostics-diagnostics-stats-servers.png
        diagnostics-diagnostics-short-0.png diagnostics-diagnostics-short-1.png
        diagnostics-diagnostics-short-2.png diagnostics-diagnostics-short-3.png
        diagnostics-diagnostics-short-4.png diagnostics-diagnostics-report.png)
add_ui_scenario(sites GROUPS all
    ARGS -ui-preview @PREFIX@ -ui-preview-docs -theme "Throned Graphite" -ui-preview-sites
    EXPECTED sites-sites.png)
add_ui_scenario(sites-error GROUPS all
    ARGS -ui-preview @PREFIX@ -ui-preview-docs -theme "Throned Graphite" -ui-preview-sites -ui-preview-sites-error
    EXPECTED sites-error-sites.png)
add_ui_scenario(hover GROUPS all
    ARGS -ui-preview @PREFIX@ -ui-preview-docs -theme "Throned Graphite" -ui-preview-hover
    EXPECTED hover-hover-card.png)
add_ui_scenario(subscription GROUPS all
    ARGS -ui-preview @PREFIX@ -ui-preview-docs -theme "Throned Graphite" -ui-preview-subscription
    EXPECTED subscription-announce.png subscription-subscription.png
        subscription-subscription-in-place.png subscription-subscription-muted.png
        subscription-announce-dismissed.png subscription-group-editor.png
        subscription-program-menu.png subscription-start-with.png)
add_ui_scenario(group-menu GROUPS all
    ARGS -ui-preview @PREFIX@ -ui-preview-docs -theme "Throned Graphite"
        -ui-preview-many-groups -ui-preview-group-menu
    EXPECTED group-menu-group-menu.png)
add_ui_scenario(many-groups GROUPS all
    ARGS -ui-preview @PREFIX@ -ui-preview-docs -theme "Throned Graphite" -ui-preview-many-groups
    EXPECTED many-groups-window.png)
add_ui_scenario(favorites GROUPS all
    ARGS -ui-preview @PREFIX@ -ui-preview-docs -theme "Throned Graphite" -ui-preview-favorites
    EXPECTED favorites-favorites.png)
add_ui_scenario(main-running-unselected GROUPS all
    ARGS -ui-preview @PREFIX@ -ui-preview-docs -theme "Throned Graphite" -ui-preview-running-unselected
    EXPECTED
        main-running-unselected-panel-opening.png main-running-unselected-panel-closing.png
        main-running-unselected-search-filtered.png main-running-unselected-closed.png
        main-running-unselected-logs.png main-running-unselected-logs-menu.png
        main-running-unselected-logs-menu-in-place.png main-running-unselected-graph.png
        main-running-unselected-window.png main-running-unselected-menu.png
        main-running-unselected-menu-in-place.png main-running-unselected-stop-button.png
        main-running-unselected-update-downloading.png main-running-unselected-update-preparing.png
        main-running-unselected-update-ready.png main-running-unselected-update-error.png)
add_ui_scenario(subscription-long GROUPS all
    ARGS -ui-preview @PREFIX@ -ui-preview-docs -theme "Throned Graphite"
        -ui-preview-subscription -ui-preview-long-announce
    EXPECTED subscription-long-announce.png subscription-long-subscription.png
        subscription-long-subscription-in-place.png subscription-long-subscription-muted.png
        subscription-long-announce-dismissed.png subscription-long-group-editor.png
        subscription-long-program-menu.png subscription-long-start-with.png)
add_ui_scenario(diagnostics-light GROUPS all
    ARGS -ui-preview @PREFIX@ -ui-preview-docs -theme "Throned Graphite"
        -ui-preview-diagnostics -ui-preview-diagnostics-light
    EXPECTED diagnostics-light-diagnostics-entry.png diagnostics-light-diagnostics-overview.png
        diagnostics-light-diagnostics-site.png diagnostics-light-diagnostics-app.png
        diagnostics-light-diagnostics-app-rule.png diagnostics-light-diagnostics-stats.png
        diagnostics-light-diagnostics-stats-proxyonly.png diagnostics-light-diagnostics-stats-servers.png
        diagnostics-light-diagnostics-short-0.png diagnostics-light-diagnostics-short-1.png
        diagnostics-light-diagnostics-short-2.png diagnostics-light-diagnostics-short-3.png
        diagnostics-light-diagnostics-short-4.png diagnostics-light-diagnostics-report.png)
add_ui_scenario(favorites-compact GROUPS all
    ARGS -ui-preview @PREFIX@ -ui-preview-docs -theme "Throned Graphite"
        -ui-preview-size 1000x700 -ui-preview-favorites
    EXPECTED favorites-compact-favorites.png)
add_ui_scenario(route-simple GROUPS smoke all
    ARGS --route-editor-preview -theme "Throned Graphite" --output @OUTPUT@
    EXPECTED route-simple.png)
add_ui_scenario(route-advanced GROUPS smoke all
    ARGS --route-editor-preview -theme "Throned Graphite" --advanced --output @OUTPUT@
    EXPECTED route-advanced.png)
add_ui_scenario(route-detail GROUPS all
    ARGS --route-editor-preview -theme "Throned Graphite" --advanced --detail --output @OUTPUT@
    EXPECTED route-detail.png)
add_ui_scenario(route-paste GROUPS all
    ARGS --route-editor-preview -theme "Throned Graphite" --paste --paste-sample --output @OUTPUT@
    EXPECTED route-paste.png)
add_ui_scenario(route-russian GROUPS all
    ARGS --route-editor-preview -lang ru -theme "Throned Graphite" --output @OUTPUT@
    EXPECTED route-russian.png)
foreach (_theme IN ITEMS midnight ocean violet ember)
    add_ui_scenario("theme-${_theme}-main" GROUPS all
        ARGS -ui-preview @PREFIX@ -ui-preview-docs -theme "${_theme}" -ui-preview-selection
        EXPECTED "theme-${_theme}-main-selected.png" "theme-${_theme}-main-cleared.png")
    add_ui_scenario("theme-${_theme}-routes" GROUPS all
        ARGS --route-editor-preview -theme "${_theme}" --output @OUTPUT@
        EXPECTED "theme-${_theme}-routes.png")
endforeach ()
add_ui_scenario(update-prompt GROUPS all
    ARGS --update-prompt-preview -lang en -theme "Throned Graphite" --output @OUTPUT@
    EXPECTED update-prompt.png)

get_property(_all_scenarios GLOBAL PROPERTY UI_SCENARIO_NAMES)
set(_selected)
foreach (_requested IN LISTS SCENARIOS)
    if (_requested STREQUAL "all" OR _requested STREQUAL "smoke")
        foreach (_candidate IN LISTS _all_scenarios)
            get_property(_groups GLOBAL PROPERTY "UI_${_candidate}_GROUPS")
            if (_requested IN_LIST _groups)
                list(APPEND _selected "${_candidate}")
            endif ()
        endforeach ()
    elseif (_requested IN_LIST _all_scenarios)
        list(APPEND _selected "${_requested}")
    else ()
        message(FATAL_ERROR "Unknown UI scenario '${_requested}'. Available: ${_all_scenarios}")
    endif ()
endforeach ()
list(REMOVE_DUPLICATES _selected)

file(REMOVE_RECURSE "${OUTPUT_DIR}/actual" "${OUTPUT_DIR}/diff" "${OUTPUT_DIR}/logs")
file(MAKE_DIRECTORY "${OUTPUT_DIR}/actual" "${OUTPUT_DIR}/diff" "${OUTPUT_DIR}/logs")
if (UPDATE_BASELINES)
    file(MAKE_DIRECTORY "${BASELINE_DIR}")
endif ()

set(_failed 0)
set(_manifest_entries)
foreach (_scenario IN LISTS _selected)
    get_property(_args GLOBAL PROPERTY "UI_${_scenario}_ARGS")
    get_property(_expected GLOBAL PROPERTY "UI_${_scenario}_EXPECTED")
    set(_prefix "${OUTPUT_DIR}/actual/${_scenario}")
    list(GET _expected 0 _first_expected)
    set(_output "${OUTPUT_DIR}/actual/${_first_expected}")
    list(TRANSFORM _args REPLACE "^@PREFIX@$" "${_prefix}")
    list(TRANSFORM _args REPLACE "^@OUTPUT@$" "${_output}")
    if (PREVIEW_FONT)
        list(APPEND _args -ui-preview-font "${PREVIEW_FONT}")
    endif ()

    message(STATUS "UI scenario: ${_scenario}")
    execute_process(
        COMMAND "${THRONED_EXECUTABLE}" ${_args}
        RESULT_VARIABLE _result
        OUTPUT_VARIABLE _stdout
        ERROR_VARIABLE _stderr
        TIMEOUT ${TIMEOUT_SECONDS})
    file(WRITE "${OUTPUT_DIR}/logs/${_scenario}.log" "${_stdout}${_stderr}")

    set(_scenario_status passed)
    if (NOT "${_result}" STREQUAL "0")
        set(_scenario_status failed)
        set(_failed 1)
        message(SEND_ERROR "${_scenario} exited with '${_result}'. See ${OUTPUT_DIR}/logs/${_scenario}.log")
    endif ()

    foreach (_image IN LISTS _expected)
        set(_actual "${OUTPUT_DIR}/actual/${_image}")
        set(_baseline "${BASELINE_DIR}/${_image}")
        set(_diff "${OUTPUT_DIR}/diff/${_image}")
        if (NOT EXISTS "${_actual}")
            set(_scenario_status failed)
            set(_failed 1)
            message(SEND_ERROR "${_scenario} did not produce ${_image}")
            continue()
        endif ()

        if (UPDATE_BASELINES)
            file(COPY_FILE "${_actual}" "${_baseline}" ONLY_IF_DIFFERENT)
        elseif (COMPARE_BASELINES AND EXISTS "${_baseline}")
            if (NOT DEFINED SNAPSHOT_COMPARE OR NOT EXISTS "${SNAPSHOT_COMPARE}")
                set(_failed 1)
                message(SEND_ERROR "A baseline exists, but throned_snapshot_compare was not found")
                continue()
            endif ()
            execute_process(
                COMMAND "${SNAPSHOT_COMPARE}"
                    --expected "${_baseline}" --actual "${_actual}" --diff "${_diff}"
                    --channel-tolerance "${CHANNEL_TOLERANCE}"
                    --max-different-ratio "${MAX_DIFFERENT_RATIO}"
                RESULT_VARIABLE _compare_result
                OUTPUT_VARIABLE _compare_stdout
                ERROR_VARIABLE _compare_stderr)
            file(APPEND "${OUTPUT_DIR}/logs/${_scenario}.log"
                "\ncompare ${_image}:\n${_compare_stdout}${_compare_stderr}")
            if (NOT "${_compare_result}" STREQUAL "0")
                set(_scenario_status failed)
                set(_failed 1)
                message(SEND_ERROR "Snapshot changed: ${_image}; diff: ${_diff}")
            endif ()
        elseif (COMPARE_BASELINES AND REQUIRE_BASELINES)
            set(_scenario_status failed)
            set(_failed 1)
            message(SEND_ERROR "Missing baseline: ${_baseline}")
        endif ()
    endforeach ()

    # A geometry report beside a capture is compared as text: it answers what the
    # image cannot (a label that no longer fits, a box that moved) and its failures
    # are readable without opening a picture.
    foreach (_image IN LISTS _expected)
        string(REGEX REPLACE "\.png$" ".json" _report "${_image}")
        set(_actual_report "${OUTPUT_DIR}/actual/${_report}")
        set(_baseline_report "${BASELINE_DIR}/${_report}")
        if (NOT EXISTS "${_actual_report}")
            continue()
        endif ()
        if (UPDATE_BASELINES)
            file(COPY_FILE "${_actual_report}" "${_baseline_report}" ONLY_IF_DIFFERENT)
        elseif (COMPARE_BASELINES AND EXISTS "${_baseline_report}")
            execute_process(
                COMMAND "${CMAKE_COMMAND}" -E compare_files "${_baseline_report}" "${_actual_report}"
                RESULT_VARIABLE _report_result
                OUTPUT_QUIET ERROR_QUIET)
            if (NOT "${_report_result}" STREQUAL "0")
                set(_scenario_status failed)
                set(_failed 1)
                message(SEND_ERROR "Layout changed: ${_report}\n  expected: ${_baseline_report}\n  actual:   ${_actual_report}")
            endif ()
        endif ()
    endforeach ()

    list(APPEND _manifest_entries "    { \"name\": \"${_scenario}\", \"status\": \"${_scenario_status}\" }")
endforeach ()

list(JOIN _manifest_entries ",\n" _manifest_body)
file(WRITE "${OUTPUT_DIR}/manifest.json"
    "{\n  \"platform\": \"${_platform}\",\n  \"results\": [\n${_manifest_body}\n  ]\n}\n")

if (_failed)
    message(FATAL_ERROR "UI scenarios failed. Actual images, diffs and logs: ${OUTPUT_DIR}")
endif ()
message(STATUS "UI scenarios passed. Results: ${OUTPUT_DIR}")
