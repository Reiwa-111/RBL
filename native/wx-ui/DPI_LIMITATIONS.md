# wx frontend DPI behavior and verification limits

The Windows wxWidgets 3.2.11 resource embeds the PerMonitorV2 manifest. Main
frame dimensions are persisted in DIP. AUI perspectives additionally persist
`AUI/DpiScale`; loading scales only the serialized `bestw`, `besth`, `minw`,
`minh`, `maxw`, `maxh`, `floatw`, `floath`, and the pixel-size integer in
`dock_size(dir,layer,row)=size`. The three integers inside `dock_size(...)`
are validated and preserved as topology, not scaled. Negative AUI size sentinels are preserved. Dock topology
(`dir`, `layer`, `row`, `pos`, `proportion`) and floating `floatx`/`floaty`
coordinates are not scaled. Floating pane rectangles are instead clamped to
the work area of the monitor containing their saved position, falling back to
the frame's monitor if that position is no longer on a display.

On Windows, the frame attempts to load all icon sizes from
`assets/rblstudio.ico` through `wxIconBundle`; the generated 32x32 icon remains
the fallback. Linux continues to use the generated icon.

`wxEVT_DPI_CHANGED` is allowed to propagate, then AUI size fields and the
known custom DIP metrics are reapplied asynchronously with `CallAfter`.
This ordering is a conservative attempt to avoid changing AUI state during
wxWidgets' native DPI event dispatch; it has not been validated with a live
multi-monitor GUI session.

- On every DPI transition, the project-tree glyphs are rerasterized into
  `FromDIP(16,16)` image-list cells, and the Problems/Output image lists are
  rebuilt from bitmap bundles at the new DIP size. Toolbar tool bitmap bundles
  are requested again at the destination `FromDIP(18,18)` size. This avoids
  retaining the original monitor's fixed-size raster assets, but their visual
  sharpness and spacing still require real-display review.
- The generated fallback application icon is rerasterized at `FromDIP(32,32)`
  on non-Windows platforms. Windows uses the multi-size `.ico` bundle; its
  fallback is also DIP-sized.
- The Settings dialog and Output/Console sizer spacing now use DIP-converted
  padding. Standard wx file/text dialogs rely on wxWidgets/native DPI behavior.
- Floating `floatx`/`floaty` coordinates are intentionally not DPI-scaled;
  clamping prevents wholly off-screen placement but cannot infer the user's
  intended monitor after display topology changes.
- AUI perspective parsing is deliberately fail-closed: malformed recognized
  size fields or implausible scale ratios reject the saved perspective and
  allow the normal default-layout recovery path to run.
- Automated static checks cover the manifest, AUI perspective grammar,
  DIP-converted metrics, image-list regeneration and toolbar bitmap refresh.
  No live 125/150/175/200-percent or multi-monitor visual acceptance has been
  performed because the available display is 96 DPI. Verify startup,
  old-profile migration, floating panes, toolbar/tree/list/editor/dialog
  metrics, and repeated 100%-to-200% monitor moves before treating mixed-DPI
  behavior as fully validated.

These changes are a conservative DPI improvement, not a claim of complete
mixed-DPI support.
