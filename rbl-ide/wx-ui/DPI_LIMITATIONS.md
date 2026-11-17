# wx frontend DPI behavior and verification limits

The Windows wxWidgets 3.2.11 resource embeds the PerMonitorV2 manifest. Main
frame dimensions are persisted in DIP. AUI perspectives additionally persist
`AUI/DpiScale`; loading scales the serialized `bestw`, `besth`, `minw`, `minh`,
`maxw`, `maxh`, `floatw`, `floath`, and the pixel-size integer in
`dock_size(dir,layer,row)=size`. The three integers inside `dock_size(...)` are
validated and preserved as topology, not scaled. Negative AUI size sentinels
are preserved. Dock topology (`dir`, `layer`, `row`, `pos`, `proportion`) and
floating `floatx`/`floaty` coordinates are not scaled. Floating pane rectangles
are clamped to the work area of the monitor containing their saved position,
falling back to the frame's monitor if that position is no longer on a display.

On Windows, the frame loads icon sizes from `assets/rblstudio.ico` through
`wxIconBundle`; the generated 32x32 icon remains the fallback. Linux continues
to use the generated icon.

`wxEVT_DPI_CHANGED` is allowed to propagate, then AUI size fields and known
custom DIP metrics are reapplied asynchronously with `CallAfter`. After loading
and clamping the scaled AUI perspective, `ApplyDpiMetrics` calls
`manager_.Update()` so native floating frames and dock geometry are committed
in the deferred DPI pass.

- On each DPI transition, project-tree glyphs are rerasterized into
  `FromDIP(16,16)` image-list cells, Problems/Output image lists are rebuilt from
  bitmap bundles at the new DIP size, and toolbar bitmap bundles are requested
  again at `FromDIP(18,18)`.
- The generated fallback application icon is rerasterized at `FromDIP(32,32)`
  on non-Windows platforms. Windows uses the multi-size `.ico` bundle.
- Settings and Output/Console sizer spacing use DIP-converted padding. Standard
  wx file/text dialogs rely on wxWidgets/native DPI behavior.
- Floating `floatx`/`floaty` coordinates are intentionally not DPI-scaled;
  clamping prevents wholly off-screen placement but cannot infer the user's
  intended monitor after display topology changes.
- AUI perspective parsing is fail-closed: malformed recognized size fields or
  implausible scale ratios reject the saved perspective and use normal layout
  recovery.
- Static checks cover the manifest, AUI perspective grammar, DIP-converted
  metrics, image-list regeneration and toolbar bitmap refresh.

## Windows acceptance — 2026-11-17

- Initial live multi-monitor acceptance used two 1920x1080 displays at 96 DPI
  (100%). The GUI and worker started; the main frame moved between displays; the
  Project pane detached to the secondary display and its floating position
  survived a restart.
- Saved-layout migration was exercised with stored AUI scale 0.8 and current
  scale 1.0. The floating pane changed from 624x558 to 780x698 and was clamped
  to the 1030px work-area boundary. Deliberately off-screen coordinates
  recovered to `{x=1140,y=332,w=780,h=698}`.
- The user subsequently confirmed that the real Windows mixed-DPI scenario was
  tested successfully. Windows mixed-DPI acceptance is therefore **PASS**.
  Exact physical scale pairs and a detailed theme/dialog checklist were not
  independently captured in this work log; no specific percentages are claimed.

## Scope boundaries

The Windows mixed-DPI result does not establish interactive Linux/Flatpak GUI,
portal, or Flathub acceptance. Those remain separate platform/distribution
scopes. These records do not claim universal compatibility with every display
topology or Linux desktop.
