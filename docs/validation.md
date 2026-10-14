# Validation and delivery status

## 1.1.0 updates and avatar ESP preview

Delivered [1.1.0 portable ZIP](../releases/Noctra-1.1.0-windows-x64.zip), [EXE](../dist/Noctra.exe), [signed bootstrap manifest](../releases/update-v1.1.0.json), [public verifier key](../config/update-public-key.txt), and [release instructions](updates.md). The DPAPI-protected publisher private key is outside the workspace under the current Windows user's LOCALAPPDATA; it is not packaged, logged or uploaded. No GitHub upload/push/release publishing was authorized or performed.

- Normal launches check the configured public GitHub release endpoint asynchronously. Check/download/install controls are on Misc / Version and Config / Version, with explicit restart confirmation. Versions and application logic changes need a new tested signed release, not just a local code edit; offset data is resolved separately at runtime. Existing 1.0.0 copies need one manual upgrade.
- ECDSA P-256 signed payload, exact SHA-256/size, expiry, repository URL/architecture/product restrictions and monotonic sequence checks protect the update path. A copied helper validates the original process, waits for exit, replaces the EXE with a backup and rolls back on failed new-menu startup. No credentials, external scripts, elevation or downloaded ZIP execution are used.
- Avatar images now preview box, health, illustrative skeleton, tracer, name and distance toggles. Static PNG box/skeleton guides are approximate and are labelled as style previews, not reconstructed live joints. Live geometry remains available; no game-reader behavior changed.
- Startup offset discovery is implemented through the configured HTTPS feed. The detected Roblox build selects its pinned offsets document; the required schema is validated before use, and a valid local cache is retained for offline startup. Unknown or unavailable builds remain disabled, and all existing runtime identity, geometry and health gates remain intact.

Final checks:
- Release/Debug build and all four CTest targets passed; 41 updater, 160 core/geometry, 231 UI and 37 avatar assertions. UI includes real restart-consent clicks and preview toggle draw regression checks.
- Real helper process integration passed signed replacement/restart and deliberately failed signed payload rollback in isolated folders with isolated LOCALAPPDATA. Original profiles and EXE were not modified by those cases.
- Real GitHub discovery returned `No public release available yet`. GitHub 404 can also mean a private/missing repository; public feed availability and end-to-end remote download/install remain blocked until a public signed release is published. Discovery does not prove a deployed feed.
- ZIP exact five-file allowlist/CRC passed, packaged EXE byte equality passed, signed manifest hash/size matches the 1,287,680-byte EXE. ZIP is 657,993 bytes. Only standard Windows DLL imports remain.
- Extracted 1.1.0 EXE ten-second/four-tab smoke passed (exit 0, mask 15, no input events). Zero ready/avatar frames: no final live-game/avatar alignment claim. No speedup/performance claim.
- Native window check initially failed because the menu was minimized (`-32000` coordinates), not because its drag handler changed. Restored that window and reran unmodified probe: borderless/topmost/header/control/resize checks passed at 96 DPI. The app was responding and left open.
- A UI click test initially failed after adding a separate app-version line moved the restart button. Adjusted test click to the actual button row; no consent assertion was weakened. Earlier mixed-auto compile failure was fixed; both builds reran.
- Executable is unsigned (manifest signing is not Authenticode). Another-PC, clean Windows, full remote update, final static-avatar visual alignment, and updated game support remain unverified.

The older sections below are historical snapshots. Their 1.0.0 paths, sizes and absence-of-updater descriptions are superseded by 1.1.0 above.

## Portable share release

Delivered [Noctra-1.0.0-windows-x64.zip](../releases/Noctra-1.0.0-windows-x64.zip) and the standalone [Noctra.exe](../dist/Noctra.exe). CMake now sets the MSVC runtime to static `/MT` (Debug `/MTd`) before adding libraries, including ImGui. CPack uses explicit install entries for exactly five package files: the EXE, recipient READ-ME and three third-party license notices. The user confirmed permission to redistribute both supplied menu fonts, so their current appearance is preserved; that confirmation is not an independent rights audit.

Verification after the packaging changes:
- Release and Debug builds succeeded; all three CTest targets passed in both configurations.
- `dumpbin /dependents` shows only Windows system DLLs, with no VCRUNTIME/MSVCP/debug runtime imports. D3D11, D3DCompiler_47, WinHTTP and COM/WIC still rely on Windows components.
- The ZIP is 624,516 bytes; its Windows x64 EXE is 1,217,536 bytes. ZIP CRCs passed, the exact five-file allowlist passed, and the packaged EXE is byte-identical to `dist/Noctra.exe`.
- Extracted the ZIP to a separate folder and launched its EXE with that folder as working directory. The ten-second/four-tab smoke passed (exit 0, tabs mask 15, zero automatic input events). There were zero ready live frames, so this proves native rendering/startup/packaged assets, not live avatar/ESP/aim behavior.
- Release is Authenticode **NotSigned**; no signing identity was supplied. SmartScreen/antivirus outcomes and a clean Windows/other-PC run are unverified. No security settings were changed.
- Current packaging does not claim universal Roblox, custom-rig or custom-team coverage. No installer, code signing, publishing or upload was performed.
- SHA-256 checksums for the EXE and ZIP are in [SHA256SUMS.txt](../releases/SHA256SUMS.txt). Package contents omit private profiles, diagnostics, local account details, source and debug symbols.

## Roblox profile avatar preview

The character pane now prefers an official full-body profile avatar PNG for the local player's account name, with a button to refresh it and a toggle back to the live local R6/R15 geometry preview. Name/health and the preview still require a fresh local character. The static profile image includes avatar appearance such as clothing/accessories when Roblox's returned thumbnail does; it is not animated, not guaranteed to match an experience-modified avatar, and does not replace the live pose. It uses public endpoints without login credentials or cookies. Network/decode failure keeps the geometry preview available and displays a concise fallback label.

Implementation safeguards: worker-thread HTTPS fetch; username is read from the already validated local-player snapshot; official username-to-ID resolution validates the returned account name; full-body thumbnail JSON validates exact target ID and state; redirects/auth/cookies disabled; returned CDN URL constrained to HTTPS `*.rbxcdn.com`; WinHTTP timeouts and 4 MiB body/PNG cap; WIC decoded dimensions limited to 1024×1024; immutable D3D11 texture upload occurs on the UI thread; GPU texture is retained by COM ownership and cached per account for the session. No profile image or credentials are written to disk.

Checks for the avatar change:
- Release build succeeded with `/W4 /WX /permissive-`.
- Release CTest passed core, menu, and avatar targets.
- Avatar tests cover account-name and thumbnail-response schema/identity rejection, unsafe CDN URLs, `Pending` versus `Completed`, bounded PNG decoding, pixel format, loader idle/shutdown, and malformed/truncated/oversized image handling.
- Live official Roblox API test for the local username resolved its account and decoded a 720×720 PNG; 37 avatar assertions passed. This verifies API/network/parser/WIC path, **not** D3D11 image rendering in the reopened UI.
- Native app was not open during this pass and Roblox had no active validated game session, so live GPU preview and final screenshot are still not user-interface-verified. The updated executable needs to be opened in an active supported scene for visual confirmation.

Primary sources: [Roblox Thumbnails API](https://create.roblox.com/docs/cloud/reference/domains/thumbnails), [Roblox Users API](https://create.roblox.com/docs/cloud/reference/domains/users), [Microsoft WinHTTP sessions](https://learn.microsoft.com/en-us/windows/win32/winhttp/winhttp-sessions-overview), and [Windows Imaging Component](https://learn.microsoft.com/en-us/windows/win32/wic/-wic-about-windows-imaging-codec). The returned thumbnail is static; a full-textured animated live model would require a different renderer and asset pipeline.

## Latest quick cleanup: icons and version-only chrome

Per the user's screenshot request, icon rendering now centers the actual glyph ink bounds, correcting the supplied font's unusual bearings. The sidebar local/profile/hotkey block, preview rig/help/runtime/FPS/session copy, module debug/help paragraphs, and startup footer status were removed. The footer/watermark and Misc/Config Version pages display the detected client build (fallback NOCTRA 1.0.0). The preview, controls, safety gates, and game logic remain intact; essential profile action errors and confirmations stay on the profile page. Disarm moved to Misc / General.

Verification after these edits:
- Release and Debug builds/CTest passed; 160 core/preview assertions and **220 UI assertions** including absence of runtime status copy and presence of Version-page text.
- A new test initially crashed because ImGui logging was started outside a frame/window. The test harness was repaired to capture within an owned test window, then both configurations reran successfully; application error handling was not weakened.
- Native ten-second/four-tab smoke passed (`menu_tabs_mask=15`, 653 ready frames, zero input events, exit 0).
- Rebuilt native window passed borderless/topmost/header/corner/control/resize checks at 96 DPI and was left open. Final user visual approval of the icon centering is pending.

The remaining sections are historical and describe earlier UI copy that this cleanup supersedes.

## Earlier layout repairs and live character preview

Latest changes repair the slider label/track collisions shown in the user's screenshots and inconsistent draw-list font sizing. Slider labels and tracks now occupy separate rows; public ImGui sliders restore keyboard and clamped Ctrl-click numeric input. Custom navigation/checkboxes have visible keyboard focus. Explicit draw-list font/size and improved text contrast/clipping keep headers, icons, and footer aligned.

The dummy `Player_247` preview is removed. [preview.cpp](../src/preview.cpp) builds fitted root-relative body boxes from the fresh local entity's pose, sizes, and rotations, depth-sorts faces, and provides real skeleton nodes. This remains available as the interactive live geometry fallback. The pane now prefers the official static profile-avatar PNG (see the newer avatar verification section above); it does not claim to reproduce animated in-game appearance or experience-modified outfits. R15 proxy R6-named parts are suppressed when UpperTorso is present.

Module usability changes:
- Aim: explicit off/waiting/unknown-team/armed feedback and accessible response controls. No aim algorithm or offsets changed.
- Visuals: master/element feedback, numeric color entry and reset accent.
- Misc: snapshot age and standard-team status plus one-click disarm.
- Config: selected-profile highlighting, last save/load feedback, and confirmation before reset. Schema remains 2.

Final verification:
- Release and Debug builds/Ctest pass: **160 core/preview assertions + 181 UI assertions**.
- Tests include fitted geometry/depth order, translation independence, rotation, invalid dimensions/rotation, fresh/local-only data selection, real ImGui navigation/checkbox/slider input, numeric clamping, reset confirmation, and every module/subpage at 100%, 125%, 150%, 200% scale.
- Native ten-second smoke passes (`menu_tabs_mask=15`, exit 0, zero input events): frame median 9.818 ms / p95 10.911 ms. It ran in unsupported Phantom Forces with zero ready frames; it is a menu-only measurement, not a live ESP/aim benchmark.
- Native borderless/topmost/header/corner/control/resize checks passed on the rebuilt window, responding at PID 33216 at verification.
- Phantom Forces diagnostic returned exit 2 with zero validated character entities. The game remains unsupported; no custom adapter was introduced.
- After the user switched to TTK, the delivered executable's live diagnostic returned exit 0, `ready=1`, `entities=5`. **The user confirmed both repaired layout and live preview work.** TTK custom teams remain unavailable, so team-safe aiming remains blocked.
- New aim/game-specific features, real multi-monitor DPI, full mesh rendering, and live aiming behavior remain unverified/unimplemented as previously documented.

Sources researched and applied:
- [Pinned ImGui API](https://raw.githubusercontent.com/ocornut/imgui/v1.91.9b/imgui.h) and [FAQ](https://raw.githubusercontent.com/ocornut/imgui/v1.91.9b/docs/FAQ.md): child sizing, explicit draw-list font sizing, navigation-enabled InvisibleButton, public SliderFloat behavior.
- [Roblox CFrame](https://create.roblox.com/docs/reference/engine/datatypes/CFrame), [rig documentation](https://create.roblox.com/docs/studio/rig-builder), and [R6/R15 adapter](https://create.roblox.com/docs/characters/r6-to-r15-adapter): pose/rotation representation and proxy-part caveats.

The following sections are historical; the latest checks above supersede older counts/menu limitations, not unsupported game/team functionality.

## Earlier menu replacement: enQ

The latest request replaced the previous dark menu with the supplied `enQ dx9.rar` source design; Figma work was stopped. The layout, custom-widget geometry, and embedded fonts were ported onto the existing ImGui/DX11 implementation without changing the client reader, aim logic, live ESP drawing, or profile schema.

Final checks for this menu replacement:
- Release and Debug builds pass with `/W4 /WX`; CTest passes both core and menu targets.
- **57 core assertions and 30 UI assertions** pass. UI tests feed real ImGui mouse events through icon/subtab navigation, ESP/box checkboxes, and smoothing-slider interactions; all 12 module/subpage combinations draw, and minimum-size element fit and enlarged preview sizing are checked.
- The native ten-second smoke completed: `elapsed_seconds=10.009`, `menu_tabs_mask=15`, exit 0, zero automatic input events. Menu frame median 10.003 ms / p95 11.583 ms; zero ready live frames, so this is not a live ESP/aim benchmark.
- The actual rebuilt window (PID 33152 at verification) passed caption removal, always-on-top style, full client area, native header drag/corner resize/control hit testing, and real resize round-trip at 96 DPI. It was responding and was left open for review.
- Embedded font atlas builds; fonts belong to their own ImGui context. Original archive files remain unchanged. See [enQ notes](../third_party/enq/README.md) for provenance and the unverified independent font redistribution rights.
- Final user visual approval and multi-monitor DPI remain unverified. Earlier live/game-specific limitations below still apply.

The history below describes the previous implementation and earlier checks; its interrupted smoke/topmost checks are superseded for the **menu only** by the passing checks above.

## Artifacts

- [Release executable](../dist/Noctra.exe): Windows x64 native application, C++20.
- [Build configuration](../CMakeLists.txt), [source](../src/main.cpp), [core](../src/core.cpp), [read-only client](../src/client.cpp), [menu/ESP](../src/ui.cpp).
- [Usage instructions](../README.md).
- [Figma MCP server-entry JSON](../config/figma-mcp.json) and [setup limitations](figma-mcp.md).
- Dear ImGui MIT notice copied to [dist/Dear-ImGui-LICENSE.txt](../dist/Dear-ImGui-LICENSE.txt).

Original menu source was left untouched. No commits, pushes, deployment, administrator elevation, or system execution-policy changes were performed.

## Verified

### Build and automated tests

- MSVC x64 Release and Debug builds succeeded with project-owned C++ `/W4 /WX /permissive-`.
- Final core regression executable: **57 assertions passed**; CTest passes in both configurations.
- Tests cover projection/invalid matrices/clipping, oriented bounds, standard skeleton topology, filtering, target retention and respawn identity, unknown-team fail-closed aiming, frame-rate-independent smoothing, deadzone, radial movement cap, long-frame suppression, offset-schema validation, bounded addresses/collections, real inaccessible `ReadProcessMemory`, strict profile schema, save/load round-trip, malformed/duplicate/unknown/oversized profiles, and invalid-save preservation.
- Helper Python files compile; Figma configuration parses as JSON and has the expected remote URL.

### Initial live subset validation

Running binary: `version-ae421f0582e54718`, file/product version `0.742.0.7421053`, SHA-256 `11a30ada177d15a9ec1c1dc30957b28b46839c9c6407e53be6138d2f12f6f2fe`.

User-provided offsets target `version-cec3ad5889b447cf`. Three globals lie in the installed binary's readable/writable `.data` and resolve to readable pointers in the initial game process. The required chain resolved to a DataModel, player service, local player, camera, dimensions 1920×1080, finite nonzero projection matrix, and standard character data.

Before spawning, character pointers were null. After the user entered a playable scene, three samples showed nine characters with coherent head sizes/positions and health/max-health values; observed positions changed between samples. This verifies only the used subset, not all 1,674 values or every property interpretation. The executable still revalidates session state and blocks unavailable or incompatible offset documents.

The native C++ `--diagnose` exited successfully in the initial playable scene. A later diagnostic after Roblox restarted did not pass; see below.

### User-observed interface

The user confirmed live ESP looked correct and boxes stayed attached to other players when jumping. Camera-relative vertical movement is expected. This confirmation occurred before the final topmost/aim revisions.

### Earlier native window verification

An earlier rebuilt borderless version passed a direct native-window probe:

- caption removed / full client area;
- header `HTCAPTION` drag hit test;
- corner resize hit test;
- client hit testing over custom controls;
- actual resize round-trip at 96 DPI.

Two earlier four-tab smoke runs exited successfully. These do not establish final-user approval or prove every control was clicked.

## Failures, interruptions, and unverified checks

- Initial build had a mixed-type `auto` declaration; corrected, rebuilt, and affected checks rerun successfully.
- Initial caption-style check failed because Windows retained caption bits; corrected with explicit post-creation style update, then the native probe passed.
- Windows execution policy blocked the optional screenshot script. No attempt was made to bypass/change that policy. Final screenshots were not captured.
- One owned application's graceful close did not exit within five seconds. The identified owned process was stopped after checking its executable path. Shutdown lifecycle code was repaired; final graceful-close regression is unverified because later interactive runs were closed before inspection.
- Reading generated diagnostic artifacts was blocked by the host; blocked contents were not retrieved through another route. Claims are based on command exit results, directly observed earlier diagnostic output, and user feedback.
- **Latest smoke attempts failed:** windows exited after approximately 4.9 and 3.7 seconds, before the required ten seconds/four tabs (masks 7 and 3). The smoke assertions were not weakened. They are interruptions/incomplete final smoke coverage, not passing tests.
- The final reopened application no longer existed when the native probe ran. Therefore the final **always-on-top native style check could not complete**, despite the compiled `WS_EX_TOPMOST` implementation and unchanged earlier borderless geometry logic.
- Roblox restarted to a new PID with the same binary fingerprint, no game window, and null DataModel/VisualEngine globals. Live diagnostics then returned exit 2. The reader was repaired to report initialization waiting rather than a generic bounds error. Final completion recheck of the delivered executable confirmed `diagnostic_status=Waiting for Roblox scene initialization`, `ready=0`, `entities=0`, exit 2. No active NOCTRA process was present; the menu was not reopened.
- Final game-input/aiming verification was skipped. Mouse input acceptance/sensitivity/first-person behavior and the latest deadzone/cap tuning are not claimed live-tested. Automated test modes send no aim input.
- No final representative live benchmark completed. Partial menu-only runs measured around 9.95–9.98 ms median / 10.78–11.32 ms p95 frame durations, but had **zero ready live frames** and were interrupted. These are not live ESP/aim performance results or speedup claims.
- R6/R15 topology has automated coverage; separate live R6/R15, Rivals, respawn, teleport, restart recovery, multi-monitor DPI, capture, and custom camera tests remain unverified.

## TTK team root cause and current behavior

In the inspected TTK session (observed place `114234929420007`), ten players all had `Team=null`, `Neutral=true`, and TeamColor `1`. Standard Team/TeamColor filtering cannot distinguish allies from enemies. No custom TTK team adapter was recovered or implemented.

Team check now blocks aiming when local standard team membership is not validated and excludes neutral/null-team candidates when classification is available. ESP reports unclassified players instead of pretending it knows their affiliation. Disabling Team check includes all nonlocal alive targets. That is explicit user control, not repaired game-specific team detection.

## Figma status

Gravity catalog lookup returned no integration. Official Figma documentation was researched; a remote MCP entry and setup guide were prepared. **No actual Figma MCP connection exists** in this client session, no OAuth was completed, and no Figma frames/assets were imported. Connector-capable client support and user sign-in are required.

## Acceptance comparison

Delivered: native C++ NOCTRA-style menu, independent DX11 overlay, standard-rig ESP elements, hold-key target selection/ordinary mouse-input implementation, persistent validated profiles, exact-build gating, diagnostics, and tests.

Not delivered/confirmed: undetected operation, all-game support, game-specific Rivals/TTK adapters, actual Figma connector setup, visibility/occlusion filtering, prediction, capture hiding, and final live aiming/performance verification. The project is **not represented as completion of those requirements**.

## Sources used for UI refinements

- [Microsoft custom window frames](https://learn.microsoft.com/en-us/windows/win32/dwm/customframe)
- [WM_NCCALCSIZE](https://learn.microsoft.com/en-us/windows/win32/winmsg/wm-nccalcsize)
- [WM_NCHITTEST](https://learn.microsoft.com/en-us/windows/win32/inputdev/wm-nchittest)
- [Dear ImGui pinned Win32/DX11 example](https://github.com/ocornut/imgui/blob/v1.91.9b/examples/example_win32_directx11/main.cpp)

## Recheck checklist

Open the final executable, verify topmost/borderless appearance and all controls, run the native probe while the window remains open, spawn in a supported scene, run diagnostics/benchmark uninterrupted, then manually validate aiming and focus suppression. TTK enemy classification needs a separately verified custom adapter; do not infer teams from identical/default colors.
