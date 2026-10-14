# NOCTRA native external

Windows x64 C++20 / Win32 / DirectX 11 / Dear ImGui application. The current menu is ported from the user-supplied **enQ DX9** source: light-gray shell, icon rail, category column, compact controls, and preview. Its layout/custom-widget styling and embedded fonts are adapted to the existing pinned ImGui/DX11 renderer; the old DX9 backend and modified ImGui core are not used. Original archives/menu sources are unchanged. See [enQ integration notes](third_party/enq/README.md).

## Share with friends

Send [Noctra-1.2.0-windows-x64.zip](releases/Noctra-1.2.0-windows-x64.zip), not the source/build folder. Recipients should extract it and open `Noctra.exe`; the archive includes a short run guide and all bundled third-party license notices. The standalone executable is also at [dist/Noctra.exe](dist/Noctra.exe); keep the license notices with redistributed copies. Package/executable hashes are in [SHA256SUMS.txt](releases/SHA256SUMS.txt).

The Windows x64 Release uses a statically linked MSVC runtime (`/MT`), including ImGui and project libraries. Recipients do not need Visual Studio, CMake, Python, or a matching Visual C++ Redistributable; Windows 10/11 and a DirectX 11-compatible GPU/driver are required. This is an unsigned portable build, not an installer. Security software may warn: verify the sender/hash and do not disable protections. Live features resolve offsets for the detected Roblox client build. 1.2.0 adds an experimental Phantom Forces geometry-only adapter alongside standard rigs; final live PF rendering is unverified.

To rebuild the share ZIP after a verified Release build:

```bash
cpack --config build/CPackConfig.cmake -C Release
```

CPack installs only the executable, recipient guide and three license notices. No source, profiles, secrets, account-specific diagnostics or debug symbols are included. The supplier confirmed redistribution permission for the supplied fonts; this is not an independent legal audit. See [recipient guide](docs/distribution.txt) and [validation](docs/validation.md).

## Application updates

1.1.0 adds automatic launch-time checks against public GitHub Releases in `brobro12235/Noctraexternal`. Download and installation require recipient action through **Misc / Version** or **Config / Version**, including restart confirmation. Signed manifests, exact payload hashes, monotonic sequences, same-volume backup/replacement and menu-startup rollback protect the update path. No GitHub token or signing private key is shipped.

Friends with 1.0.0 need **one manual upgrade** to 1.1.0. Changes reach users only after you publish a tested, signed release; saving edits does not remotely modify their computers. Nothing has been uploaded/published in this task. See [publishing instructions](docs/updates.md). Roblox offsets are resolved at startup from the configured HTTPS feed and cached beside the executable as `offsets.json` plus `offsets.version`. The reader fails closed when a build is unavailable or runtime validation fails.

## Run

Open [dist/Noctra.exe](dist/Noctra.exe). No administrator elevation is requested. Live modules start disabled.

- **Insert**: show/hide the menu. The menu/game must have focus for this hotkey.
- The menu is **always-on-top**, borderless, draggable by the left side of its header, and resizable at its edges. `_` minimizes; `X` closes. Default size is 805×520; minimum size is 805×480 at 96 DPI.
- **Visuals → ESP → Player ESP**: master switch. Boxes, names, health, R6/R15 skeletons, tracers, distance labels, and accent color are independently configurable.
- **Aim → Targeting → Aim assist**: enable, hide the menu, focus Roblox, then hold the selected key. Defaults to Mouse 2. Selection is inside a screen-space FOV radius, in pixels. Distances are studs, not meters.
- **Aim → Response** contains smoothing/gain/deadzone; **Aim → Filters** and **Visuals → Filters** contain Team check. **Visuals → Appearance** contains color/FOV/watermark; **Misc → Interface** contains menu opacity/key.
- Smoothness controls a time-based response; Mouse gain compensates for game sensitivity. Aim deadzone suppresses small adjustments; maximum input step caps the total movement magnitude. No universal sensitivity calibration is claimed.
- Aim stops when the menu opens, focus changes, data is older than 150 ms, the activation key releases, or compatibility checks fail. A frame longer than 100 ms produces no aim movement.
- The **Your character** pane loads the local player's public Roblox profile avatar image (including visible hair, clothing, and accessories) using the in-game username and official Roblox username/full-body-thumbnail APIs. This is a static profile thumbnail—not an animated pose or guaranteed copy of an experience's modified appearance. It makes unauthenticated HTTPS requests without Roblox cookies or credentials; loading is asynchronous, cached per account for the session, and offers **Refresh avatar**. Failed/unavailable images fall back to the live local R6/R15 body-geometry preview. **Live geometry** toggles back when the avatar is loaded; drag it to rotate or reset the view. The avatar image also previews boxes, name, health, skeleton, tracer and distance toggles. Its box/skeleton are approximate style guides, not live joints; switch to Live geometry for real pose. Name/health and avatar/geometry are hidden when local character data is stale. Phantom Forces has no verified local-character identity, so its experimental anonymous geometry does not populate this pane; TTK's standard character geometry was previously live-confirmed.
- Sliders now have separate label/track rows, keyboard adjustment, and Ctrl-click numeric input with range clamping. Custom checkboxes/navigation expose keyboard focus; columns are regression-tested at 100–200% scale.
- **Misc → General → Disarm modules** turns off Aim and ESP. **Visuals → Appearance** supports numeric color entry and accent reset. Profile reset asks for confirmation.
- The menu/footer and optional watermark show the version instead of runtime status/debug text. Misc and Config have a **Version** page; removed help/status paragraphs remain documented here. Sidebar icons are centered by visible glyph bounds.
- **Config** saves/loads named profiles in `%LOCALAPPDATA%\Noctra\profiles`. Profile schema is 2. Invalid/incomplete profiles are rejected. Loading always disarms aiming. Profiles are not automatically loaded at startup.

### Team check: important

The reader supports **standard Roblox Team membership only**. In the inspected TTK session every player had a null Team, Neutral=true, and identical TeamColor. There is no reliable standard-team classification there.

When Team check is enabled but standard teams cannot be validated, **aim selection is blocked**. ESP continues showing unclassified players. The Team check tooltip explains the restriction; runtime diagnostics remain available through `--diagnose` rather than cluttering the menu. Disabling Team check allows all nonlocal alive characters to be considered; it does not magically identify enemies. A custom TTK adapter or manual ignore list is **not implemented**.

### Compatibility

Offsets follow the detected Roblox client build. On startup the reader loads that build's pinned offsets document, validates every field it uses, and atomically caches the result beside `Noctra.exe`. A valid cache is used when the service is temporarily unavailable; runtime identity checks still fail closed if the running client does not match the data. Each session must still pass identity, parent, collection, string, camera, geometry, and health checks; fresh offsets do not bypass those gates. The reader uses ordinary read-only Windows process access; no injection, drivers, memory writes, or anti-cheat bypasses.

**1.2.0 experimental Phantom Forces (place 292439477):** a bounded adapter validates the inspected two-folder replicated-character layout, six tiny proxy parts and coherent nested visual meshes. Accepted anonymous geometry can draw boxes, tracers and distance labels. Names, health, skeleton, team classification, local-model ownership and aiming are unavailable; aiming is blocked regardless of Team check. Geometry may include allies; excluding nearby models is not a verified self filter. Empty or changed layouts draw nothing. Final live rendering remains **unverified** because the latest scene had zero replicated models. Misc / Version and Config / Version show model/rejection counts; `--diagnose` distinguishes empty scenes from rejected geometry. This adapter does not scan Lua modules, guess identities, broaden the offset schema or cover every PF place/mode.

R6/R15 standard body parts are supported by the renderer. Custom rigs, custom team systems, custom cameras, streaming omissions, and individual experiences can require additional adapters. Universal game support, Rivals/TTK-specific coverage, and 'undetected' operation are **not guaranteed**.

## Build and checks

Prerequisites: Windows x64, CMake 3.24+, Git, and Visual Studio C++ Build Tools with a Windows SDK. CMake fetches Dear ImGui at a pinned commit (`v1.91.9b`) and nlohmann/json at a pinned commit for strict avatar-service response parsing; their license notices are copied beside the executable.

```bash
cmake -S . -B build -G 'Visual Studio 18 2026' -A x64
cmake --build build --config Release --parallel 6
ctest --test-dir build -C Release --output-on-failure
cmake --build build --config Debug --parallel 6
ctest --test-dir build -C Debug --output-on-failure
```

For Visual Studio 2022 use `-G 'Visual Studio 17 2022'` with a separate build directory. Release output is `dist/Noctra.exe`; Debug output is `build/Debug/Noctra.exe`. Project-owned C++ compiles with `/W4 /WX /permissive-`.

Diagnostic modes (run from this workspace):

```bash
./dist/Noctra.exe --diagnose
./dist/Noctra.exe --smoke-test
./dist/Noctra.exe --benchmark
python tools/native_ui_check.py
python tools/check_client.py --teams
```

- `--diagnose`: wait up to ten seconds for a ready scene and write `artifacts/native-diagnostic.txt`; exit 2 when unavailable.
- `--smoke-test`: cycle all five native tabs and render for ten seconds, with automated aiming disabled. Exit 2 if closed before all tabs are exercised. This tests rendering/lifecycle, not real clicks on every control.
- `--benchmark`: twenty-second renderer/reader sample with ESP/skeleton/tracers rendered, automated aiming disabled. Requires a ready live scene; exit 2 otherwise. Reports median/p95 frame/read duration and CPU relative to one core. With an unfocused game, overlay work is measured but not displayed over it. This is not a universal game-input performance benchmark.
- `native_ui_check.py`: checks ownership, caption removal, topmost style, hit testing, and a real resize round-trip while one native menu is open. It interacts only with NOCTRA.
- `check_client.py`: standalone bounded read-only candidate diagnostic; optional team-field summary. Python is needed only for these helper scripts, not the executable.

The optional [native_capture.ps1](tools/native_capture.ps1) script captures only the NOCTRA window. Windows execution policy blocked it on this host; no policy was changed.

## Figma (paused)

Figma work was stopped at the user's request in favor of the supplied enQ source. The earlier prepared files are retained but no integration is activated.

[config/figma-mcp.json](config/figma-mcp.json) is a credential-free remote-server entry, **not an installed/connected integration**. See [docs/figma-mcp.md](docs/figma-mcp.md). This Freebuff session has no MCP connector manager exposed; a frame link or exported design can also be used for a manual native implementation.

## Remaining limitations

- Visibility/occlusion filtering, prediction, capture hiding, verified custom player/team adapters beyond the experimental PF geometry reader, and a manual player ignore list are not implemented.
- Native ESP alignment was user-confirmed before the menu replacement; game logic was not changed. The repaired layout and live local-character preview were user-confirmed in TTK. Final aiming behavior, capture behavior, real multi-monitor DPI, and all game modes remain unverified.
- Current builds pass Release/Debug CTest with core/preview, UI, and avatar assertions; the avatar suite tests strict API identity/schema validation, CDN URL restrictions, bounded PNG decoding, and async loader idle behavior. A live official-API check resolves the in-game username `potassiumnn` and decodes its 720×720 full-body PNG. This confirms the service and decoder, not the live menu GPU render. See [docs/validation.md](docs/validation.md) for current UI/live-window checks and limitations. The smoke ran before switching from unsupported Phantom Forces to TTK and had zero ready live frames; no representative live ESP/aim performance claim is made.
- See [docs/validation.md](docs/validation.md) for exact verified/failed/blocked checks. This is a usable implementation with explicit gaps, **not completion of every requested 'all games' feature**.
