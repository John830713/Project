# WebAgent

HTTP bridge to the Tampermonkey Web Agent. A C++ replacement for the Flask
`server.py` that used to serve `D:\Tampermonkey` (now removed): browsers keep
running the same `agent/core.js` + station modules, unchanged, and talk to this
server instead of Python.

## Files

| File | Role |
|------|------|
| `WebAgent.module.ini` | Manifest: class `WebAgentModule`, links `ws2_32` |
| `WebAgentModule.h/cpp` | `IFeatureModule`: config, start/stop/restart, auto-start, context menu |
| `HttpServer.h/cpp` | Minimal HTTP/1.1 server: listener thread, exact + prefix routes, CORS |
| `WebAgentApi.h/cpp` | All endpoints + session/queue/report state, static JS, dashboard |
| `WebAgentTypes.h/cpp` | `HttpRequest`/`HttpResponse` value types, URL decode |
| `lang/zh-TW.ini` | `[Common]` config labels + `[WebAgent]` menu labels |

## Configuration

`Config/Config_WebAgent.ini`, section `[WebAgent]`:

| Key | Default | Meaning |
|-----|---------|---------|
| `Enabled` | `1` | Master switch; off ⇒ no server, no menu items |
| `AutoStart` | `1` | Start on launch / after settings apply |
| `Port` | `8921` | Listen port (matches `send_cmd.py`'s default) |
| `BindHost` | `0.0.0.0` | Bind address |
| `ResourceRoot` | `D:\Tampermonkey` | Root of the JS assets (`agent/`, `modules/`); missing ⇒ the module hides itself entirely |
| `ShowConsole` | `0` | Reserved (per-server console not implemented) |

`ApplyConfig()` runs once at startup and after the settings dialog: it always
stops then restarts the server so a changed port/host/root takes effect, and
keeps a manually-started server alive when `AutoStart` is off. `AgentDir` is
`ResourceRoot\.agent` (dumps, hidden selectors).

### Machines without the resource root

`ResourceRoot` is the folder that holds the browser-side scripts (normally
`D:\Tampermonkey`). When it does not exist the machine must look as if WebAgent
was never shipped, so `WebAgentModule::IsAvailable()` returns false and:

- `StartServer()` refuses and logs `StartServer skipped: resource root not found
  (<path>).` — no listener, no port conflict.
- `ModuleManager` skips unavailable modules in `GetContextMenuItems()` /
  `GetMenuGroups()`, so neither the tray nor the pet `Function` submenu grows a
  `Tampermonkey Web Agent` group. `GetContextMenuItems()` itself also returns
  nothing, so the check holds even if a new host forgets to filter.
- `SettingsDialog` drops unavailable modules, so there is no settings tab for a
  module that cannot run here.
- `debug console` → `modules` marks it `[unavailable: hidden from menus/settings]`.

The check is `std::filesystem::is_directory()`, re-evaluated on every call: menus
and the settings dialog are rebuilt on open, so creating the folder later makes
everything appear without a restart. An **empty** folder counts as available —
the server starts and every asset 404s — so deploy `agent/` + `modules/`, not
just the directory.

To run the assets from another location, point `ResourceRoot` at it by hand in
`Config/Config_WebAgent.ini` (the settings tab only appears once that path
exists) and restart `Project.exe`.

### Context menu

| Item | Behaviour |
|------|-----------|
| `Start Web Agent` / `Stop Web Agent` | Toggles the listener; label follows `ServerRunning()` |
| `Restart Web Agent` | `StopServer()` + `StartServer()`, greyed out while stopped |
| `Open Dashboard` | Opens `http://localhost:<port>/dashboard`, greyed out while stopped |


## Endpoints

Served on `BindHost:Port` with `Access-Control-Allow-Origin: *`.

| Method | Path | Purpose |
|--------|------|---------|
| GET/POST | `/ping` | Liveness (`{"status":"ok"}`) |
| POST | `/hello` | Register a browser session (URL dedup, purges stale commands) |
| POST | `/tag` | Associate tags with a session (204) |
| GET/POST | `/poll` | Deliver the next command; POST body is the previous command's report |
| POST | `/report` | Fire-and-forget result report (204, used by `sendBeacon`) |
| POST | `/command` | Queue one command (`_session` filter, queue cap 100 → 429) |
| POST | `/commands` | Queue a batch |
| GET | `/status` | Sessions, queue size, report count, task info |
| GET | `/metrics` | Totals: sessions active/tracking, queue, uptime |
| GET | `/queue` | Pending command list |
| GET | `/reports` | Report ring (`limit`, `drain`, `session`, `cmd`) |
| GET | `/results` | Tail of `task_results.jsonl` |
| GET | `/hidden` · POST · DELETE | Hidden-selector list persisted under `.agent/browser/` |
| GET/POST | `/dump` | Element dump + base64 screenshot extraction |
| GET | `/tasks` · POST `/task/stop` · POST `/task/<name>` | Task-engine stubs (deferred) |
| GET | `/agent.core.js`, `/universal.loader.user.js` | Only the JS surface the loader fetches (legacy `/loader-core.js`, `/agent.user.js`, `/agent.loader.user.js` were dropped with the Python path) |
| GET | `/modules` | `modules.json` with per-script `@version` injected |
| GET | `/serve/<name>` | Module script by name |
| GET | `/` , `/dashboard` | Self-contained control panel |

### Protocol parity

Deliberately mirrors the retired Python server (`server.py`, removed from
`D:\Tampermonkey`) so the existing userscripts and `send_cmd.py` work without
edits:

- Commands are delivered with `tagged` and `poll_ms`; the `/poll` body report is
  merged into the ring before delivery.
- Tracking URLs (length > 500 or a host in `kTrackingDomains`) never receive
  commands and are excluded from the main queue logic.
- A command may carry `_session`; it is parked in a pending-filtered list and
  only delivered to that session (the key is stripped before delivery).
- Session TTL is 300 s; reports are a 500-entry ring.
- Query values are percent-decoded (Flask `request.args` parity).

## Deferred

- **Task engine**: `/tasks` returns `{"available":[]}`, `/task/<name>` is 404,
  `/task/stop` is a no-op. Python generators become a C++ state machine in a
  later phase; `/reports` and `/results` already work.
- **Console**: `ShowConsole` is stored but unused.

## Networking notes

- `winsock2.h` must precede `windows.h`; `WebAgentModule.h` includes
  `HttpServer.h` first to keep that order. Avoid `s_host`/`s_addr`/`s_net`
  variable names (`inaddr.h` macros).
- `HttpServer` runs one listener thread and handles each connection inline
  (`Connection: close`); `Stop()` closes the listen socket to wake `accept`.
- Static handlers add a trailing `\n` and send core scripts with `no-cache`,
  matching Flask.

## Verification

`Debug/WebAgent/WebAgentTest.cpp` — 63 assertions: URL decode, response
builders, router (exact/prefix/OPTIONS), static serving and traversal
rejection, and the full protocol (hello → command → poll → report, filtered
delivery, tracking quarantine, metrics, hidden, dump).

## See also

- [Modules/INDEX.md](../INDEX.md)
- [spec.md §14](../../resources/reference/design/spec.md) — architecture rationale
