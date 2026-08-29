<p align="center">
  <img src="res/public/Throned.png" width="96" alt="Throned">
</p>

<h1 align="center">Throned</h1>

<p align="center">
  A Qt desktop proxy client powered by sing-box and Xray —<br>
  my personal, unofficial fork of <a href="https://github.com/throneproj/Throne">Throne</a>.
</p>

<p align="center">
  <a href="https://github.com/troshkindm/throned/releases"><img alt="Releases" src="https://img.shields.io/github/v/release/troshkindm/throned?style=flat-square&color=3b82f6"></a>
  <a href="LICENSE"><img alt="License" src="https://img.shields.io/badge/license-GPL--3.0-blue?style=flat-square"></a>
  <img alt="Platforms" src="https://img.shields.io/badge/platforms-Windows%20%7C%20Linux-lightgrey?style=flat-square">
</p>

<p align="center">
  <img src="docs/ui-preview/main-en.png" width="860" alt="Throned main window">
</p>

---

## What Throned adds

Everything upstream Throne does, plus the following. Each item is a fork
addition, not an upstream feature.

**Routing**

- Simple and Advanced modes over one rule document — cards by rule kind, or the raw ordered list where the first match wins.
- Paste a whole rule list as plain text; bare lines and sing-box spellings (`domain_suffix`, `process_name`, `rule_set`) both work.
- Application picker backed by installed applications, running processes, or a manually chosen executable.
- Right-click a live connection to turn it into a rule — domain, subdomains, process, executable or address, to proxy, direct or block.
- Send a rule bucket through a *named profile or chain*, not just proxy/direct/block.
- Quick menu on the status bar: active profile, where unmatched traffic goes, and a switch that turns the profile's rules off entirely.
- Rule values are trimmed before use, and references that point nowhere are reported instead of failing the router silently.
- Unknown imported fields survive a round trip as `Preserved JSON`.

**TUN and DNS**

- Windows TUN self-loop guard: traffic recaptured on the TUN peer is dropped before it can loop, while DNS to the peer stays hijacked. Derived from the configured subnet, so custom ranges are covered.
- Transition guard (Windows): a WFP filter held by the core covers the gap between stopping one profile and starting the next, where traffic would otherwise take the physical interface.
- Your DNS settings can be applied over a full-config profile's own `dns` section — and are kept out when doing so would break it.
- Rule-set, GeoIP and GeoSite downloads follow the active proxy over a dedicated authenticated local inbound, instead of depending on a profile's `final` outbound.

**DPI bypass**

- Its own screen: TLS spoof method, decoy SNI, QUIC blocking, and the rule sets it applies to — also available per routing rule.

**Testing and monitoring**

- UDP latency and jitter probe with its own column, menu action and configurable target.
- Ping monitor on the traffic graph: up to three targets, the direct path measured beside the proxy one, and latency spikes flagged.
- Batch URL test, speed test and outbound-IP resolution over a multi-row selection.

**Interface**

- Frameless window with native chrome (QWindowKit), five themes, one set of semantic color tokens across every screen.
- Log view with a level filter, wrapped lines on a hanging timestamp gutter, and auto-scroll.
- Group tabs show subscription usage and expiry.
- Status strip is a caption/value grid with fixed columns, so a long name or a fast transfer no longer shoves it around.

**Operating it**

- **Copy Diagnostics** — one paste with version, OS, TUN and routing state and the recent log, secrets masked.
- Background update check announced in the tray. Nothing downloads or installs on its own.
- Control CLI over a local socket, for a person or a script.

---

## Interface

Routing has a **Simple** mode that sorts rules into application, domain,
rule-set and address cards, and an **Advanced** mode that exposes the real
ordered rule list.

<p align="center">
  <img src="docs/ui-preview/routes-simple-en.png" width="820" alt="Routing profile editor">
</p>

<p align="center">
  <img src="docs/ui-preview/connection-rule-menu.png" width="700" alt="Rule from a connection">
</p>

### Themes

Five built-in themes, switchable in **Settings → Appearance**. The background
ramp stays near-neutral in all of them and the chroma budget goes to the accent,
which keeps text edges crisp.

| Midnight | Graphite |
| --- | --- |
| <img src="docs/ui-preview/theme-midnight.png" alt="Midnight"> | <img src="docs/ui-preview/theme-graphite.png" alt="Graphite"> |

| Ocean | Violet | Ember |
| --- | --- | --- |
| <img src="docs/ui-preview/theme-ocean.png" alt="Ocean"> | <img src="docs/ui-preview/theme-violet.png" alt="Violet"> | <img src="docs/ui-preview/theme-ember.png" alt="Ember"> |

> Screens are rendered from the application itself. Every server, host and path
> is placeholder data using the reserved documentation ranges from RFC 5737.

---

## Control CLI

A running Throned can be driven from the command line. The command travels to
the open window over a local socket, runs against the same database the
interface uses, and the answer comes back on stdout.

```sh
throned --cli status
throned --cli route add example.com --via proxy
throned --cli route app add discord.exe --via direct
throned --cli route rules            # the ordered rule list; first match wins
```

Every command is also addressable as JSON, replying `{"ok":true,"data":{…}}` or
`{"ok":false,"error":"…"}`:

```sh
throned --cli '{"cmd":"routing.set_default","outbound":"proxy"}'
throned --cli '{"cmd":"logs","lines":50,"contains":"reject"}'
```

`routing.export` hands over the whole profile losslessly and `routing.import`
takes an edited one back; `{"cmd":"schema"}` describes the command surface and
the rule format, both generated from the code that parses them. Routing edits
restart the core — pass `"apply": false` to batch several and finish with
`routing.apply`.

The socket is restricted to the current user; anything able to reach it can
change where traffic goes. `throned --cli help` prints the whole reference.

---

## Downloads

Stable builds are on the
[Releases](https://github.com/troshkindm/throned/releases) page.

| Platform | Package |
| --- | --- |
| Windows x64 | Installer EXE (recommended), portable ZIP |
| Linux x64 / ARM64 | Portable ZIP |
| Debian / Ubuntu x64 / ARM64 | DEB, bundled Qt (recommended) or system Qt |
| Fedora / openSUSE x64 / ARM64 | RPM, bundled Qt (recommended) or system Qt |
| Windows ARM64, legacy Windows | Planned |
| macOS | Build from source; upstream-compatible, not CI-tested here |

TUN mode needs administrator privileges on Windows and elevated network
capabilities on Linux.

Throned inherits Throne's protocol support: VLESS, VMess, Trojan, Shadowsocks,
SOCKS, HTTP(S), TUIC, Hysteria, Hysteria2, AnyTLS, ShadowTLS, Snell,
WireGuard/AmneziaWG, SSH, Mieru, NaiveProxy, Juicity, TrustTunnel, custom
outbounds and configs, chains, and extra cores.

On first launch an existing Throne configuration is copied in when no Throned
database exists yet, and `throne://` links keep working.

---

## Building

The authoritative recipes are in
[.github/workflows](.github/workflows) — they build `ThronedCore`, the Qt
application and the packages in clean runners. Expected toolchain: Go 1.26.x,
CMake + Ninja, Qt 6.11.x, Protobuf 31.x, and MSVC on Windows or GCC on Linux.

The UI preview harness builds against Qt alone, with no database, core process
or networking:

```sh
cmake -S tools/ui-demo -B build-ui
cmake --build build-ui
```

The application also renders its real screens headlessly
(`--route-editor-preview`, `-ui-preview`), which is how the screenshots above
are made. See [docs/ui-redesign.md](docs/ui-redesign.md).

---

## About this fork

I maintain Throned to fix problems that affect me directly, test the changes on
Windows and Linux, and publish installers without waiting for a particular
upstream release. Upstream changes are merged from
[throneproj/Throne](https://github.com/throneproj/Throne) periodically, and
fork patches are kept small enough to rebase, retire, or propose upstream.

AI-assisted tools are used extensively while writing, reviewing and documenting
changes. Releases are tested before publishing, but this is a personal
best-effort project — not an official Throne build and not a promise of support.
For the upstream project and its support channels, use
[Throne](https://github.com/throneproj/Throne). Throned is provided as-is,
without warranty.

Versioning is ordinary [SemVer](https://semver.org/), and each release note
states which Throne version or commit it is based on. Bug reports should include
the Throned version, OS, TUN settings, routing profile, and the log around the
problem.

Built on [Throne](https://github.com/throneproj/Throne),
[sing-box](https://github.com/SagerNet/sing-box),
[Xray-core](https://github.com/XTLS/Xray-core), Qt, and the other projects listed
in the source tree. Licensed under [GPL-3.0](LICENSE).
