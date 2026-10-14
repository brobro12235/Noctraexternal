# enQ menu source integration

Source supplied by the user: `C:\Users\ethan\Downloads\enQ dx9.rar`, root `enQ`.

Inspected reference files:
- `examples/example_win32_directx9/main.cpp`: light-gray 805×480 shell, 50-pixel icon rail, 160-pixel category column, 280-pixel controls column, preview, and embedded font setup.
- `imgui_widgets.cpp`: custom tab/subtab buttons, checkbox geometry/animation, and compact slider geometry.
- `imgui.cpp`: column separators and child-panel headings.

The implementation in [src/ui.cpp](../../src/ui.cpp) ports that design to the existing upstream Dear ImGui v1.91.9b / Win32 / DirectX 11 application. It uses public ImGui APIs rather than replacing the upstream core with the archive's old modified fork. No DX9/D3DX9 installation is needed.

Included archive assets:
- [byte.h](byte.h): embedded `gilroyfont` and `icon` font arrays, unchanged from the supplied archive. Atlas ownership is disabled for these static arrays, and each ImGui context uses only its own fonts.
- [LICENSE.txt](LICENSE.txt): the archive's bundled Dear ImGui MIT notice, also copied to the output directory as `enQ-LICENSE.txt`.

The archive did not include separate font-rights documentation. The bundled ImGui MIT notice should not be assumed to establish independent redistribution rights for the embedded fonts. For the portable share release, the user explicitly confirmed permission to redistribute both supplied fonts and requested retaining them. This confirmation is not an independent rights audit or a license grant; retain your permission records before wider distribution.

Menu changes:
- Light-gray shell and monochrome enQ controls; NOCTRA identity retained in the footer.
- Four working modules (Aim, Visuals, Misc, Config) and three relevant subpages per module.
- Original placeholder Chams/world tabs were not exposed as nonfunctional features.
- Existing settings, profile schema, game reader, aim logic, and live ESP drawing are preserved.
- Screenshot-driven revisions separate slider labels/tracks and use public ImGui sliders for keyboard/numeric entry. Draw-list text uses explicit font sizes; checkboxes/navigation expose keyboard focus.
- The preview now uses fresh local-character snapshot geometry (pose/proportions) rather than a dummy; it is not a textured-avatar renderer.
- Native window remains borderless, resizable, and always-on-top; default 805×520, minimum 805×480 at 96 DPI.

No files in the user's Downloads archive were modified, and no binaries or scripts from the archive were executed.
