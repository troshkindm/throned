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
  <img src="docs/ui-preview/main-en.png" width="880" alt="Throned main window">
</p>

---

## What Throned adds

Everything Throne does, plus:

**Routing**
- Simple and Advanced modes over one rule document.
- Paste a whole rule list as plain text.
- Application picker: installed apps, running processes, or any executable.
- Right-click a live connection to turn it into a rule.
- Send a rule bucket through a named profile or chain, not just proxy/direct/block.
- Quick menu on the status bar: profile, default outbound, rules on/off.

**TUN and DNS**
- Windows TUN self-loop guard.
- Transition guard: no leak in the gap between stopping and starting a profile (Windows).
- Apply your DNS settings over a full-config profile's own.
- Rule-set, GeoIP and GeoSite downloads follow the active proxy.

**DPI bypass**
- Its own screen: TLS spoof, decoy SNI, QUIC blocking, target rule sets — also per routing rule.

**Testing and monitoring**
- UDP latency and jitter probe, with its own column.
- Ping monitor on the graph: up to three targets, direct path measured beside the proxy.
- URL test, speed test and IP resolution over a multi-row selection.

**Interface**
- Frameless native window, five themes.
- Log filtered by level, on a hanging timestamp gutter.
- Subscription usage and expiry on the group tab.
- Status strip that no longer reflows as numbers change.

**Operating it**
- **Copy Diagnostics** — one paste for a bug report, secrets masked.
- Update check announced in the tray; nothing installs itself.
- Control CLI over a local socket.

<details>
<summary><b>More screenshots</b></summary>

<br>

Routing, Simple mode — rules sorted into application, domain, rule-set and
address cards:

<img src="docs/ui-preview/routes-simple-en.png" width="700" alt="Routing profile editor">

Advanced mode — the real ordered rule list, first match wins:

<img src="docs/ui-preview/routes-advanced-en.png" width="700" alt="Advanced routing">

Paste a rule list as text:

<img src="docs/ui-preview/routes-paste-en.png" width="560" alt="Paste rule list">

A rule built from a live connection:

<img src="docs/ui-preview/connection-rule-menu.png" width="600" alt="Rule from a connection">

Settings:

<img src="docs/ui-preview/settings-en.png" width="700" alt="Settings">

> Rendered from the application itself. Every server, host and path is
> placeholder data using the reserved documentation ranges from RFC 5737.

</details>

<details>
<summary><b>Themes</b></summary>

<br>

Five built-in themes, in **Settings → Appearance**. The background ramp stays
near-neutral in all of them and the chroma budget goes to the accent, which
keeps text edges crisp.

| Midnight | Graphite |
| --- | --- |
| <img src="docs/ui-preview/theme-midnight.png" width="380" alt="Midnight"> | <img src="docs/ui-preview/theme-graphite.png" width="380" alt="Graphite"> |

| Ocean | Violet | Ember |
| --- | --- | --- |
| <img src="docs/ui-preview/theme-ocean.png" width="250" alt="Ocean"> | <img src="docs/ui-preview/theme-violet.png" width="250" alt="Violet"> | <img src="docs/ui-preview/theme-ember.png" width="250" alt="Ember"> |

</details>

<details>
<summary><b>Control CLI</b></summary>

<br>

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

</details>

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
capabilities on Linux. On first launch an existing Throne configuration is
copied in when no Throned database exists yet, and `throne://` links keep
working.

<details>
<summary><b>Building</b></summary>

<br>

The authoritative recipes are in [.github/workflows](.github/workflows) — they
build `ThronedCore`, the Qt application and the packages in clean runners.

The UI preview harness builds against Qt alone, with no database, core process
or networking:

```sh
cmake -S tools/ui-demo -B build-ui
cmake --build build-ui
```

The application also renders its real screens headlessly
(`--route-editor-preview`, `-ui-preview`), which is how the screenshots above
are made. See [docs/ui-redesign.md](docs/ui-redesign.md).

</details>

---

## About this fork

I maintain Throned to fix problems that affect me directly, test the changes on
Windows and Linux, and publish installers without waiting for a particular
upstream release. Upstream changes are merged from
[throneproj/Throne](https://github.com/throneproj/Throne) periodically, and fork
patches are kept small enough to rebase, retire, or propose upstream.

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

---

## Versions

| Component | Version |
| --- | --- |
| sing-box | v1.13.20 (throneproj fork) |
| Xray-core | throneproj fork |
| sing-tun | v0.9.0-beta.2 |
| Qt | 6.11.1 |
| Go | 1.26.5 |

Exact build stamps are printed in the log at startup. Protocols come from
sing-box and Xray: VLESS, VMess, Trojan, Shadowsocks, SOCKS, HTTP(S), TUIC,
Hysteria, Hysteria2, AnyTLS, ShadowTLS, Snell, WireGuard/AmneziaWG, SSH, Mieru,
NaiveProxy, Juicity, TrustTunnel, custom outbounds and configs, chains, and
extra cores.

Built on [Throne](https://github.com/throneproj/Throne),
[sing-box](https://github.com/SagerNet/sing-box),
[Xray-core](https://github.com/XTLS/Xray-core), Qt, and the other projects listed
in the source tree. Licensed under [GPL-3.0](LICENSE).
