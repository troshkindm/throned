# UI design record

Why the interface looks the way it does. This is a record of decisions, not a
plan: the redesign it describes has shipped, and the screenshots below are
rendered from the production widgets, not from a mockup. How to render them is
in [development.md](development.md); what the scenarios cover is in
[../tests/ui/README.md](../tests/ui/README.md).

## Main window

![Main window](ui-preview/main-en.png)

The bottom bar has three explicit states: the connected profile, a
multi-selection action bar, and progress for a running batch operation. URL
test, speed test and outbound-IP resolution act on the preserved table
selection instead of replacing the connection status with an ambiguous toast.

![Selected profiles action bar](ui-preview/main-selected-en.png)

Logs wrap at the window edge with a hanging timestamp and level gutter. IPs and
ports use the warning accent; only executable names use the process accent.

## Themes

Every theme is built from the same semantic tokens in
`include/ui/setting/ThronedPalette.hpp`. The background ramp stays close to
neutral in all five: a tinted window at low luminance contrast makes text edges
read as soft, so the chroma budget is spent on the accent family instead. Each
theme keeps one clear lightness ladder — recessed surface, window, raised
control, hover, hairline — so panels separate without heavy borders.

| Midnight | Graphite |
| --- | --- |
| ![Midnight](ui-preview/theme-midnight.png) | ![Graphite](ui-preview/theme-graphite.png) |

| Ocean | Violet | Ember |
| --- | --- | --- |
| ![Ocean](ui-preview/theme-ocean.png) | ![Violet](ui-preview/theme-violet.png) | ![Ember](ui-preview/theme-ember.png) |

Any of them renders with `-theme midnight|graphite|ocean|violet|ember`, on a
normal launch as well as in a preview; the flag overrides the stored choice for
that run only.

## Settings

![Settings](ui-preview/settings-en.png)

## Routing

Simple and Advanced edit one shared versioned route document. Switching modes
never deletes, regroups or silently reorders rules.

![Simple routing](ui-preview/routes-ru.png)

Simple groups common matchers into application, domain and rule-set, process,
network and raw-rule cards. The action sidebar is a filter and a summary, not a
replacement for rule priority.

![Advanced routing](ui-preview/routes-advanced-en.png)

Advanced exposes the real ordered rule list, where the first matching rule wins.
Unknown imported fields stay as opaque JSON in their original position and carry
a visible `Preserved JSON` marker. The per-rule detail page keeps the original
lossless editor behind the ordered list:

![Rule detail](ui-preview/routes-detail-en.png)

## Why this is not one global stylesheet

The redesign was checked against the existing Qt forms, not only against the
main window. Most of it is shared foundation, but every area needed at least one
component that could not come from a palette:

| Area | Reused foundation | Component it still needed |
| --- | --- | --- |
| Basic, TUN, DNS and hotkey settings | Title bar, sidebar, form sections, fields, toggles, help text | Key-sequence recorder, validation summary |
| Groups and subscriptions | List rows, pills, contextual action bar | Update progress, per-group error state |
| Profile editors | Form sections, segmented modes, chips, raw JSON surface | Protocol-specific nested forms, secret-field treatment |
| Routing | Action sidebar, rule cards, condition chips, ordered list | Lossless rule document, application and process picker |
| Runtime and traffic statistics | Tabs, data table, status cards, time-range field | Chart card, legends, empty, loading and error states |

That is the reason the palette and the compact controls are shared
application-wide while the domain components stay local, rather than pasting one
stylesheet over every legacy `.ui` file.
