---
name: add-outbound
description: Add support for a new outbound protocol to Throned — config model, editor widget, factory registration, subscription parsing and tests. Use when asked to support a new proxy protocol or to extend an existing one's fields.
---

# Adding an outbound protocol

Take an existing protocol of similar shape as the template and mirror it
exactly. `snell` is a small one; `hysteria` has more fields; `openvpn` and
`openconnect` carry credentials and an advanced dialog.

## The files a protocol touches

| File | What to add |
| --- | --- |
| `include/configs/outbounds/<name>.h` | The field model, deriving from the common outbound base. |
| `src/configs/outbounds/<name>.cpp` | Serialisation to the core's JSON and back. |
| `src/configs/common/OutboundFactory.cpp` | The include, and the `if (type == "<name>") return new <name>();` arm. |
| `include/database/entities/Profile.h` | The include and the typed `<Name>()` accessor. |
| `include/ui/profile/edit_<name>.h`, `.ui`, `src/ui/profile/edit_<name>.cpp` | The editor widget. Existing screens keep their `.ui` file. |
| `src/ui/profile/dialog_edit_profile.cpp` | The include, the `LOAD_TYPE("<name>")` entry and the `else if (type == "<name>")` arm. |
| `src/configs/sub/SubscriptionParser.cpp` | The row mapping the type to its URI schemes and aliases. |
| `tests/` | Round-trip coverage: a share link or subscription entry in, the generated core JSON out. |

Grep the template protocol's name across `src` and `include` before starting and
again before finishing — if your protocol appears in fewer places than the
template, something is missing.

## Rules

- The generated JSON is consumed by the core process. Fields you invent are
  ignored silently, so a round-trip test is the only thing that proves the
  mapping.
- Do not put protocol-specific logic in `dialog_edit_profile.cpp` beyond the
  dispatch arm. The widget owns its own fields.
- Credentials belong in the profile entity and must not be logged or written
  into a preview database.
- New user-visible strings need entries in all four `res/translations/*.ts`
  files, edited by hand. The `lupdate` target rewrites all of them and buries
  the change.

## Finishing

Build, run `ctest`, and run `./script/lint_qt_idioms.sh`. If the editor is
reachable from a preview scenario, look at the screenshot before claiming it
renders.
