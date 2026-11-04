# VOFA2

A from-scratch, stability-first rewrite of the VOFA+ serial/TCP/UDP data
visualization tool. The design plan is kept in the assistant's plan directory
during development.

## Current state

Phase 0/1 foundation:

- `src/core`: fixed-capacity buffers, bounds-checked byte reader, SPSC ring,
  limits, float sanitisation.
- `src/protocol`: Qt-independent streaming parsers for RawData, JustFloat,
  FireWater and VOFA+ image frames.
- `src/commands`: safe legacy command-template parser/renderer (`kp1%.3f`,
  `%d`, `%s`, `%%`) with checked argument access; `OneClickSendPlanner` builds
  the ordered slider-only send plan with a configurable interval.
- `src/data`: `ChannelStore` with a SPSC frame ring, per-channel history,
  layout epochs, sanitisation and min/max range queries.
- `src/transport`: dedicated-thread serial/TCP client/TCP server/UDP worker,
  connection state machine, active TCP client selection, backpressure and
  parser integration.
- `src/app`: `AppController` (QML-facing session facade), `CommandStore`
  wiring, atomic `SettingsStore` with autosave/recovery, `ImageProvider`
  (`image://vofa/<id>?rev=…`), and the native `WaveformItem`.
- `src/app/TimePlotItem`: the rewritten time-domain oscilloscope. It renders
  through the Qt Quick scene graph (`QSGGeometryNode` line batches) instead of
  `QQuickPaintedItem`, so a 2800×1750 window no longer uploads a full texture
  every frame. Column min/max vertices are rebuilt on the GUI thread and
  refreshed at 50 Hz; the render thread only copies vertices. Interaction
  follows the 逐飞助手/ScottPlot reference: wheel zooms X and Y continuously
  around the pointer, left drag pans both axes, middle drag draws a box zoom,
  right drag zooms, right click opens the menu, hover shows a vertical
  crosshair (vertical + horizontal) and channel values, a Y readout pinned to
  the left axis at the hover height, double click resets to auto-follow +
  auto-Y. Middle click restores auto-Y. Every mouse press freezes the view
  before panning/box selection, so the selected region matches what is on
  screen. Y normalization uses bottom=0 (fixed an inverted mapping that made
  box zoom select the mirrored region).
  Right-drag zoom is live (the view follows the drag), not applied only on
  release; horizontal drag zooms X and vertical drag zooms Y independently. View modes: 自动跟随 / 显示全部 / 实时刷新停止; the current span is
  shown in the status label (the old preset dropdown was removed). Auto-Y
  keeps data at 61.8% of the axis; a Y-axis dialog sets manual start/span;
  channel value cards toggle visibility. The channel-settings dialog shows a
  clickable colour swatch per channel; clicking it opens a floating palette
  overlay (an in-dialog overlay, since a popup would be blocked by the modal
  dialog). The left connection panel is narrower (200–300 px) with its items packed
  tightly at the top (a trailing fill item keeps the connection/send blocks
  compact instead of stretching them across the panel height); the default
  window is 1600×900; secondary dialogs are larger and clamp to the screen.
  The channel value cards have a fixed height and the center column uses 2 px
  spacing, so the receive log sits directly under the cards.
  The waveform toolbar is a wrapping Flow, so higher density presets no longer
  push buttons or axis labels out of the panel; the receive log uses a
  flexible height so it cannot collide with the channel cards. The plot follows the app theme,
  including a white light-mode scope; grid and reference lines are stronger
  for readability. A visible “波形菜单” button in the waveform toolbar opens
  the same menu as right-click. The grid is 20×10 with 11 X labels
  and 5 Y labels (6 X labels on narrow windows). When there are fewer samples
  than pixels, the curve is drawn as one point per sample (smooth polyline)
  instead of repeating columns, so it no longer looks like a sawtooth. While
  hovering or dragging, both reference readouts follow the pointer: the
  moving time value is shown on the X axis and the moving value is shown next
  to the Y axis (clear of the static tick labels). They keep updating during
  pan, right-drag zoom and middle-drag box selection. Middle-drag box zoom ignores
  tiny boxes and clamps the resulting X/Y span so a gesture cannot shrink the
  view to an empty screen; a middle click restores automatic Y. Reference analysis: `逐飞助手V1.2.7_示波器功能与交互说明.md`.
- `src/app/WaveformItem`: the secondary `QQuickPaintedItem` used for the XY,
  FFT spectrum and histogram analysis modes.
- Image transmission: `SampleImageParser` transparently extracts
  `image:<id>,<fmt>,<w>,<h>,<len>\n<payload>` frames interleaved with
  JustFloat/FireWater data, with bounded headers/payloads and invalid-frame
  resync. The QML image panel shows the latest frame and frame counter.
- `src/app/App.qml`: Qt 6/QML shell with a stable adaptive three-panel layout,
  collapsible panels, connection panel (serial with data bits/parity/stop
  bits/flow control/DTR/RTS, TCP client/server, UDP, demo source), automatic
  reconnect, listen/client status, protocol and density presets plus a live
  custom density/font dialog, RX/TX/frame-rate statistics, receive log with
  pause/timestamps/auto-scroll, HEX dump view, ANSI colour (SGR fg/bold),
  line-contains filtering, UTF-8/system (GBK) decoding, send line-ending choice,
  latest channel values, image panel, CSV export, file sending, quick-send
  history with text filtering and loop sending, serial BREAK control, and a
  working “一键发送数据” dialog (per-slider checkboxes, select all/none, 100 ms
  default interval). Also includes legacy `vofa+.cmds.json`/`vofa+.tabviews.json`
  import (sliders and paired toggles) that backs up the old files and writes a
  `migration-report.json` with skipped-item reasons, a parameter editor, and
  action button/toggle widgets. Right-panel items are partitioned into named command
  groups (projects): a dropdown switches the active group, a manager dialog
  adds/renames/deletes groups and bulk-moves their contents, and the editors
  assign items to groups. One-click sending only uses the active group.
- Slider tuning is live: moving a slider sends its bound command throttled to
  33 ms, and releasing sends the final value. The behavior is configurable.
  The numeric readout beside each slider is an editable field: type any number
  (including values outside the slider's min/max) and press Enter or leave the
  field to send it; the slider thumb then clamps to its range, but the typed
  value is kept and sent exactly.
- Oscilloscope curves and background grid lines are drawn as thick triangle
  bands (2 px curves, 1.2/2 px grid) rather than GL lines, so they stay thick
  on Windows/D3D/ANGLE where `glLineWidth` is clamped to 1.
- `tests`: 61 Qt-free unit tests, 7 Qt transport tests (TCP, UDP, serial PTY,
  10k-capable connect/close stress), 28 app-controller integration tests
  (QML-callable presets, one-click, live slider send, file send, receive
  HEX/ANSI/regex/direction filter modes, send-history filter, waveform
  snapshot/window/fixed range/analysis/trigger, CSV replay, demo source,
  auto-reconnect, nested command groups, undo/redo, continuous zoom, legacy
  import + migration report, BREAK reset, window clamping), 4 libFuzzer
  targets, and a parse-throughput benchmark.
- Waveform trigger: choose channel/level/rising-or-falling edge; the view
  freezes ~0.3 s after the crossing and draws an amber trigger line. CSV
  recordings exported from the app can be re-imported, shown statically, and
  replayed with play/pause/reset and a configurable rows-per-second rate.
- A/B measurement cursors: the toolbar “游标” button freezes live scrolling
  and places two sample-locked cursors (Shift+left / Ctrl+left or drag the
  lines). A readout panel shows A/B times, Δt and per-channel A/B/Δ values;
  “跟随” or double-click resumes live display.
- Throughput: the frame ring holds 32768 frames and the UI drains it every
  16 ms (60 Hz), enough for a 20 MB/s burst of small JustFloat frames with
  zero drops (verified by `VOFA_BURST_TEST=1`). The status bar shows the
  cumulative drop counter if the consumer ever falls behind.
- Image frames are decoded on a dedicated worker thread with latest-frame
  priority and a 128 MB `QImageReader` allocation limit; the receive thread
  never decodes. The image revision only advances after decoding completes.
  End-to-end 1920×1080 JPEG decode measured at ~6.8 ms/frame (≈146 fps,
  `VOFA_IMAGE_BENCH=1`).
- i18n scaffold: UI strings use `qsTr()`; `vofa2_<locale>.qm` is loaded from
  `<exe>/translations` and `qtbase_<locale>.qm` from the Qt installation.
  `--lang <code>` forces a locale. Chinese is the source language; no English
  translation is shipped yet.
- Themes: light / dark / eye-care, with the Basic style palette and waveform
  grid following the theme. The choice persists.
- Operations: rotating file log (2 MB × 5) at `<config>/logs`, Windows
  minidumps at `<config>/dumps`, single-instance lock, portable mode (place an
  empty `portable` file next to the exe), window-screenshot saving from the
  更多 menu, and runtime screen/DPI clamping when the window moves across
  monitors or the display scale changes.

The parser and core libraries are deliberately Qt-free so they can be built and
fuzzed on Linux/WSL; the Qt application is built for Windows with Qt 6.8.3 +
MinGW 13.1.

## Build and test (Linux/WSL)

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

## Benchmark

```sh
cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-release
./build-release/tests/bench/vofa_parse_bench
./build-release/tests/bench/vofa_waveform_bench
```

`vofa_waveform_bench` measures the CPU-side 8-channel × 250k-sample min/max
decimation into 2000 pixel columns (the work done per plot refresh). On the
development machine it runs at ~1.18 ms/frame (target <2 ms CPU); GPU cost is
not measured by this benchmark.

Synthetic JustFloat benchmark on the development machine: ~254 M samples/s
(4 channels/frame, 20 MB stream, release build).

## Soak / stress

```sh
# Demo source for 60 s, monitors RSS
MODE=demo SOAK_SECONDS=60 ./scripts/soak.sh

# 8-channel JustFloat over TCP with ~0.5% corrupt/truncated bytes
MODE=tcp SOAK_SECONDS=120 ./scripts/soak.sh

# Full-day soak
MODE=tcp SOAK_SECONDS=86400 ./scripts/soak.sh

# 10,000 connect/disconnect cycles (default in ctest is 100)
VOFA_STRESS_CYCLES=10000 QTEST_FUNCTION_TIMEOUT=600000 \
  ./build-qt/tests/qt/vofa_transport_tests tcpClientConnectCloseStress

# 20 MB/s burst (1M JustFloat frames), asserts zero frame drops
VOFA_BURST_TEST=1 QT_QPA_PLATFORM=offscreen \
  ./build-qt/tests/qt/vofa_app_controller_tests tcp20mbBurstNoDrop

# 1080p JPEG end-to-end decode benchmark
VOFA_IMAGE_BENCH=1 QT_QPA_PLATFORM=offscreen \
  ./build-qt/tests/qt/vofa_app_controller_tests image1080pDecodeBenchmark
```

Dual-monitor DPI checks on Windows use `scripts/monitor_info.ps1` and
`scripts/move_window.ps1` (DPI-aware window moves between 125% and 175%
monitors); the app logs `screen changed` / `clamped window` and stays inside
the target monitor's work area.

## Fuzzing (Clang)

```sh
cmake -S . -B build-fuzz -G Ninja \
  -DCMAKE_CXX_COMPILER=clang++ \
  -DVOFA_BUILD_TESTS=OFF \
  -DVOFA_BUILD_FUZZERS=ON
cmake --build build-fuzz
./build-fuzz/tests/fuzz/fuzz_justfloat -max_total_time=60
./build-fuzz/tests/fuzz/fuzz_firewater -max_total_time=60
./build-fuzz/tests/fuzz/fuzz_image -max_total_time=60
./build-fuzz/tests/fuzz/fuzz_sample_image -max_total_time=60
```

## Packaging

```bat
D:\coding\vofa2\scripts\package_windows.bat
```

Creates `VOFA2-0.1.0-win64.zip` from `dist\`. If Inno Setup 6 is installed
(checked in Program Files and the per-user `%LOCALAPPDATA%\Programs` path) it
also compiles `installer\vofa2.iss` into
`installer\output\VOFA2-0.1.0-setup.exe`. The installer is a per-user install
(`PrivilegesRequired=lowest`, no UAC prompt), about 16 MB; silent install and
silent uninstall (installation directory fully removed) are verified.
Portable mode: create an empty file named `portable` next to
`vofa2_app.exe`; settings then live in `data\` beside the executable.

## Windows build (primary target)

Toolchain used:

- Qt 6.8.3 `win64_mingw` at `D:\Qt\6.8.3\mingw_64`
- MinGW 13.1 at `D:\Qt\Tools\mingw1310_64`
- CMake 3.30.5 and Ninja at `D:\Qt\Tools\CMake` / `D:\Qt\Tools\Ninja`

Build, test and deploy:

```bat
D:\coding\vofa2\scripts\build_windows.bat
```

The final application is `D:\coding\vofa2\dist\vofa2_app.exe` with its Qt DLLs,
QML modules and MinGW runtime alongside. The script runs all three test
executables before deploying, then trims the deployment to the Basic control
style only (the app forces `QQuickStyle::setStyle("Basic")`), drops Qt
translations, `qmltooling` and the software OpenGL fallback. The resulting
`dist` is ~51 MB.

Useful debug/test switches:

```bat
vofa2_app.exe --tcp-server 5000 --protocol JustFloat
vofa2_app.exe --tcp-client 127.0.0.1:5001 --protocol JustFloat --auto-one-click
vofa2_app.exe --demo
vofa2_app.exe --import-legacy-default --demo
vofa2_app.exe --import-legacy "C:\path\to\vofa+\140\context"
vofa2_app.exe --tcp-server 5000 --ui-preset 0 --screenshot shot.png --screenshot-quit
vofa2_app.exe --screenshot-delay 8000 --screenshot shot.png --screenshot-quit
vofa2_app.exe --qml-preset 0   rem exercise the QML app.setUiPreset() path
vofa2_app.exe --qml-reset-window
vofa2_app.exe --demo --demo-groups --select-group 项目B
vofa2_app.exe --protocol RawData --log-hex --log-timestamps
```

`--demo` runs a synthetic 4-channel 100 Hz source, which is useful for layout,
density and rendering checks without hardware. `--import-legacy-default` scans
all old `vofa+` version folders and imports the one with the most sliders and
toggles. `--ui-preset 0|1|2` selects compact/standard/comfortable; the
`界面设置` dialog additionally offers custom density/font sliders and a
`重置窗口大小` button.

`--auto-one-click` triggers the one-click send after 1.5 s, which is useful for
headless smoke tests together with `scripts/tcp_smoke_client.py`. The smoke
peer can also emit image frames:

```bat
python scripts\tcp_smoke_client.py --client 5000 --seconds 8 --image
```

`--screenshot` uses `QQuickWindow::grabWindow()` after `--screenshot-delay`
(default 2500 ms), so it works headlessly and confirms the full render path
(waveform + image + dialogs). `--ui-preset 0|1|2` selects compact/standard/
comfortable for verification.

## Qt application (WSL development)

Qt 6.8.3 was installed with `aqtinstall` under `~/Qt/6.8.3/gcc_64`. The WSL
system still needs the OpenGL/xkbcommon development headers:

```sh
sudo apt-get update
sudo apt-get install -y libgl1-mesa-dev libxkbcommon-dev libxkbcommon-x11-dev
```

Then:

```sh
cmake -S . -B build-qt -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_PREFIX_PATH=$HOME/Qt/6.8.3/gcc_64 \
  -DVOFA_BUILD_APP=ON
cmake --build build-qt
QT_QPA_PLATFORM=offscreen VOFA_SMOKE_TEST=1 ./build-qt/src/app/vofa2_app
ctest --test-dir build-qt --output-on-failure
```

The app opens the Qt/QML shell with a working connection panel, a functional
“一键发送数据” (real command templates, per-slider opt-in, configurable
interval), a native C++ waveform renderer, image transmission, CSV export,
legacy command/config import, and the parameter editor.

Verified on Windows in a 200% DPI session (2240×1435 device pixels): the
compact preset is selected automatically, the window fits the desktop, a
175 frame/s JustFloat stream renders as four smooth channels, image frames
display and update, and one-click sending emits the expected command bytes.
