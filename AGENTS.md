# SwitchU Agent Notes

This file explains the C++ architecture of SwitchU and of `nxui` so an agent can edit the owning layer instead of patching symptoms. It is intentionally organized by runtime ownership and message flow rather than by directory order.

## Why This File Is Structured This Way

- It starts from build and runtime boundaries because that is where the real ownership split is decided.
- It describes the menu, daemon, common protocol, and `nxui` separately because those are the main C++ subsystems with different constraints.
- It explains architectural choices in terms of what problem they solve, because that is more useful to an editing agent than a raw file inventory.
- It links to detailed code and existing docs instead of duplicating asset lists or setup instructions that already live elsewhere.

## Build Model

- Use `xmake` from the repository root. Do not switch to the vendored `Makefile` flows inside `lib/` or `ulaunch/` unless the task is explicitly about those subprojects.
- Prerequisites are devkitPro, devkitA64, and `xmake`.
- Default production build: `xmake f -p cross --toolchain=devkita64 && xmake`
- Homebrew `.nro` build: `xmake f -p cross --toolchain=devkita64 --homebrew=y && xmake`
- SDL2 backend build: `xmake f -p cross --toolchain=devkita64 --backend=sdl2 && xmake`
- Clean: `xmake clean`
- Outputs land under `build/cross/aarch64/<mode>/`.

### Why The Build Is Set Up This Way

- `SwitchU` is the single entry target in [xmake.lua](xmake.lua), so the same top-level build command can produce either a monolithic homebrew app or the split system build.
- Homebrew mode keeps menu development easy because the UI can run standalone.
- Sysmodule mode keeps privileged app lifecycle code isolated in the daemon while the menu stays focused on rendering and interaction.
- `nxui` and menu code are built with RTTI and exceptions enabled, while daemon code is built without RTTI. Treat the daemon as the lower-level, size-sensitive, more platform-constrained half.
- deko3d is the reference backend. SDL2 exists as a secondary path for testing and experimentation, not as the primary correctness target.

## Repo Boundaries

- [projects/daemon/src/main.cpp](projects/daemon/src/main.cpp) and neighboring headers implement the system applet side.
- [projects/menu/src/main.cpp](projects/menu/src/main.cpp) and `projects/menu/src/` implement the menu applet or the homebrew executable.
- [projects/common/include/switchu/smi_protocol.hpp](projects/common/include/switchu/smi_protocol.hpp) and [projects/common/include/switchu/smi_helpers.hpp](projects/common/include/switchu/smi_helpers.hpp) define the wire contract shared by both halves.
- `lib/nxui/` is the custom UI framework used by the menu.
- `lib/libnxtc/` is a separate metadata cache library used to avoid repeated slow NS title-control lookups on HOS 20.0.0+.
- `romfs/` contains runtime assets; `shaders/` contains GLSL sources that are compiled into `romfs/shaders/` during the build.
- `SwitchU-Themes/` and `ulaunch/` are separate surfaces. Do not make cross-cutting changes there unless the task explicitly touches them.

## High-Level C++ Architecture

SwitchU is not one process with one UI loop. It is a split system with three core layers:

1. A daemon system applet that owns application lifecycle, foreground or background state, and system-facing actions.
2. A menu applet that owns UI composition, rendering, input, settings, themeing, and user-facing state.
3. A shared protocol layer that defines the messages and storage payloads exchanged between them.

### Why The Project Is Split This Way

- The menu needs heavy rendering, textures, theme assets, and input logic. That is a bad place to also own low-level lifecycle control.
- The daemon needs to react to HOME, suspend, launch, and applet coordination without carrying the full UI stack.
- The shared protocol keeps the boundary explicit. If a change crosses daemon and menu, change the protocol first, then each side.

## Daemon Architecture

The daemon is the system coordinator. It owns application launching, resuming, foreground transitions, wake-up handling, and notification delivery to the menu.

### Main Responsibilities

- Service and subsystem initialization happens in [projects/daemon/src/main.cpp](projects/daemon/src/main.cpp).
- Application lifecycle is owned by [projects/daemon/src/app_manager.hpp](projects/daemon/src/app_manager.hpp).
- Library-applet menu lifecycle is owned by [projects/daemon/src/menu_launcher.hpp](projects/daemon/src/menu_launcher.hpp).
- The private `swu:m` queue service is implemented in [projects/daemon/src/ipc_server.hpp](projects/daemon/src/ipc_server.hpp).
- The main loop watches system events, menu state, app state, app-record changes, and wake or suspend transitions, then emits notifications.

### Important Daemon Modules

- `daemon::app` in [projects/daemon/src/app_manager.hpp](projects/daemon/src/app_manager.hpp) wraps `AppletApplication` and is the single owner of running-title state.
- `daemon::menu_la` in [projects/daemon/src/menu_launcher.hpp](projects/daemon/src/menu_launcher.hpp) wraps the menu library applet holder and its input or output storages.
- `daemon::ipc` in [projects/daemon/src/ipc_server.hpp](projects/daemon/src/ipc_server.hpp) exposes a private queue-based service for `TryPopMessage` style polling.

### Why The Daemon Is Designed This Way

- `app_manager` centralizes launch, resume, terminate, and finish detection so lifecycle state is not spread across UI code.
- `menu_launcher` isolates the library-applet plumbing from business logic. That keeps storage push or pop logic out of the main event loop.
- The daemon keeps state in small, explicit modules rather than in a deep object graph. That matches its role as an event-driven system coordinator.
- Save-data precreation is intentionally guarded and disabled by default because some titles are sensitive to it. Do not turn that back on casually.

## Menu Architecture

The menu side is a composition root around `WiiUMenuApp`. The menu executable initializes platform services, constructs `nxui::Application`, attaches `WiiUMenuApp`, and lets `nxui` drive the frame loop.

### Entry Flow

- [projects/menu/src/main.cpp](projects/menu/src/main.cpp) initializes Switch services, logging, SDL audio and fonts, and `nxtc` when running as the menu applet.
- It sets the shader base path for `nxui::Renderer` in menu mode.
- It creates `nxui::Application`, attaches `WiiUMenuApp`, initializes the framework, runs it, and shuts it down.

### `WiiUMenuApp` As Composition Root

[projects/menu/src/core/WiiUMenuApp.hpp](projects/menu/src/core/WiiUMenuApp.hpp) is the central composition class. It is intentionally broad because it wires together the menu's subsystems, but most behavior lives in dedicated helpers rather than in one giant update function.

It owns four main categories of state:

- Data and state models: `GridModel`, `AppConfig`, theme preset state, layout slots, refresh flags.
- Platform or feature adapters: `AppletLauncher`, `AppListLoader`, `IconStreamer`, `SystemMessages`, `AudioManager`, Bluetooth setup, theme package transfer state.
- `nxui` resources: fonts, textures, thread pool, theme object, and the widget-layer objects.
- Screen and overlay widgets: grid, sidebar buttons, title pill, battery, clock, dialog, settings, theme shop, user-select screen, launch animation, debug overlay.

### Why `WiiUMenuApp` Is A Composition Root

- The menu needs one place that knows how subsystems fit together.
- It should compose services and UI, but not duplicate low-level implementations that already exist in `nxui`, launcher helpers, or sidebar or theme modules.
- This structure makes it easier to keep ownership clear: edit the helper that owns the behavior, then only adjust the composition root if wiring changes.

### Menu Update Flow

The hot path in [projects/menu/src/core/WiiUMenuApp.cpp](projects/menu/src/core/WiiUMenuApp.cpp) is organized in a deliberate order:

- Resume or wake handling first, because applet foreground state gates rendering.
- Deferred background work next, such as audio startup and theme package synchronization.
- Daemon notifications next, via `AppletStorage` interactive input.
- Refresh coalescing after that, so app-record bursts do not trigger repeated expensive rebuilds.
- Touch and overlay interaction last, after the active screen stack is known.

### Why The Update Flow Is Ordered This Way

- Foreground or suspend state must be resolved before GPU work resumes.
- Heavy refresh work must be deferred and coalesced because the grid rebuild and icon updates are expensive.
- Overlay input is handled after system-state transitions so focus can be redirected safely.

### Menu Subsystems That Matter Most

- [projects/menu/src/launcher/AppListLoader.cpp](projects/menu/src/launcher/AppListLoader.cpp) fetches app metadata, prefers `libnxtc`, falls back to NS control data, and produces the model-side pending entries.
- [projects/menu/src/launcher/IconStreamer.hpp](projects/menu/src/launcher/IconStreamer.hpp) keeps compressed icon bytes in CPU memory and uploads only nearby pages to GPU memory.
- [projects/menu/src/launcher/AppletLauncher.cpp](projects/menu/src/launcher/AppletLauncher.cpp) translates UI actions into system commands and suspend or exit behavior.
- [projects/menu/src/sidebar/SidebarManager.cpp](projects/menu/src/sidebar/SidebarManager.cpp) owns sidebar button creation, asset loading, and animated icon state.
- [projects/menu/src/core/SystemMessages.cpp](projects/menu/src/core/SystemMessages.cpp) is a small thread-safe bridge that queues system actions for the main thread.
- [projects/menu/src/core/WiiUMenuAppInteraction.cpp](projects/menu/src/core/WiiUMenuAppInteraction.cpp) owns focus-root redirection, global actions, and edit-mode interaction rules.

### Why These Subsystems Are Separate

- `AppListLoader` and `IconStreamer` split data acquisition from GPU upload, which keeps loading scalable and page-aware.
- `AppletLauncher` keeps system-command code out of widgets.
- `SidebarManager` keeps themed icon assets and animation concerns out of the app root.
- `SystemMessages` gives the menu a safe way to turn asynchronous notifications into frame-bound actions.

## Shared Protocol And Cross-Applet Messaging

The shared protocol lives in [projects/common/include/switchu/smi_protocol.hpp](projects/common/include/switchu/smi_protocol.hpp) and [projects/common/include/switchu/smi_helpers.hpp](projects/common/include/switchu/smi_helpers.hpp).

### What Lives In The Protocol Layer

- POD message enums such as `MenuMessage`, `SystemMessage`, and `MenuStartMode`.
- Wire-safe payload structs such as `CommandHeader`, `LaunchAppArgs`, `SystemStatus`, `WakeSignal`, and `DaemonNotification`.
- Fixed-size AppletStorage helpers that serialize and deserialize command payloads.

### Actual Message Directions

- Menu to daemon commands use interactive out-data from [projects/menu/src/smi_commands.hpp](projects/menu/src/smi_commands.hpp).
- Daemon to menu notifications now primarily use interactive in-data carrying `WakeSignal` or `DaemonNotification` payloads.
- The private `swu:m` service still exists and is implemented in [projects/daemon/src/ipc_server.hpp](projects/daemon/src/ipc_server.hpp), with a matching client in [projects/menu/src/ipc_client.hpp](projects/menu/src/ipc_client.hpp).

### Why The Protocol Looks Like This

- The protocol uses explicit POD structs because cross-applet boundaries should be stable and easy to reason about.
- `AppletStorage` is used for the main flow because it fits applet communication and carries structured payloads without leaking framework types into either side.
- The private service remains available for queue-style polling and compatibility work, but do not move new primary notification flows back onto it without a strong reason.
- If you change a cross-boundary feature, update the protocol first, then the daemon sender, then the menu receiver.

## `nxui` Architecture

`nxui` is not just a bag of widgets. It is a Switch-focused UI framework that owns the application loop, input normalization, focus navigation, layout, rendering, resource upload, animation, localization, and a set of effect-heavy widgets.

### Core Runtime Layers

- [lib/nxui/include/nxui/Application.hpp](lib/nxui/include/nxui/Application.hpp) is the top-level loop owner.
- [lib/nxui/include/nxui/Activity.hpp](lib/nxui/include/nxui/Activity.hpp) is the screen-level abstraction attached to an application.
- [lib/nxui/include/nxui/widgets/Widget.hpp](lib/nxui/include/nxui/widgets/Widget.hpp) is the base tree node.
- [lib/nxui/include/nxui/widgets/Box.hpp](lib/nxui/include/nxui/widgets/Box.hpp) is the flex-style layout container.
- [lib/nxui/include/nxui/focus/FocusManager.hpp](lib/nxui/include/nxui/focus/FocusManager.hpp) owns focus and spatial navigation.
- [lib/nxui/include/nxui/core/Input.hpp](lib/nxui/include/nxui/core/Input.hpp) normalizes controller, touch, and gyro pointer input.
- [lib/nxui/include/nxui/core/GpuDevice.hpp](lib/nxui/include/nxui/core/GpuDevice.hpp) owns the low-level GPU device and pools.
- [lib/nxui/include/nxui/core/Renderer.hpp](lib/nxui/include/nxui/core/Renderer.hpp) owns 2D drawing, batching, offscreen capture, blur, and glass shaders.

### Why `nxui` Splits These Layers

- `Application` owns the loop and GPU lifetime so individual screens do not touch platform bootstrap code.
- `Activity` owns screen-specific behavior and can redirect input by overriding `focusRoot()`. SwitchU uses this for dialogs, settings, theme shop, user-select, and suspended states.
- `Widget` stays small and generic so feature widgets can compose behavior without a deep inheritance tree.
- `Box` implements flex-like layout so most screen composition is declarative instead of manual coordinate math.
- `FocusManager` is separate from widgets because focus policy and navigation heuristics are application-wide concerns, not per-widget implementation details.

### `nxui` Input And Focus Model

- `Application::dispatchInput()` in [lib/nxui/src/core/Application.cpp](lib/nxui/src/core/Application.cpp) updates the current `focusRoot()`, debounces D-pad and stick navigation, dispatches actions, preserves parent bubbling for non-directional buttons, and then hands touch to the focus manager.
- `FocusManager` supports two navigation models: legacy flat-grid mode and current tree-based spatial navigation.
- `Input` merges pad state, touch state, and an optional gyro-driven virtual pointer toggled by controller input.

### Why The Input Model Looks Like This

- SwitchU needs to work with controller-first navigation, direct touch, and a virtual pointer without rewriting every widget.
- Tree-based focus is more robust for overlays and layered UIs than hard-coded grid indices.
- The `focusRoot()` override is the key escape hatch that lets overlays capture all input without destroying the underlying widget tree.

### `nxui` Layout And Widget Model

- Every `Activity` owns a full-screen root `Box`.
- Widgets form a tree with geometry, opacity, margins, padding, actions, focusability, and child ownership.
- `Box` performs flex-like layout over visible children, including grow, shrink, alignment, direction, and gap behavior.
- Rendering walks the widget tree recursively. Subclasses override `onRender()` or `onUpdate()` rather than reimplementing tree traversal.

### Why The Widget Model Looks Like This

- It is lightweight enough for a custom GPU renderer and console-style UI.
- It favors composition over framework magic, which makes layout and focus behavior inspectable in code.
- It keeps per-frame update and render predictable, which matters when mixing custom effects and applet lifecycle constraints.

### `nxui` Rendering Stack

- `GpuDevice` manages the deko3d device, command buffers, descriptor memory, vertex or uniform pools, framebuffer images, and offscreen render targets.
- `Renderer` batches 2D geometry, binds shader programs, registers textures in descriptor slots, and exposes higher-level drawing and post-processing calls.
- Shaders are loaded from the configured shader base path and can fall back to `romfs:/shaders/`.
- Offscreen targets are used for blur, wave, and liquid-glass style effects.

### Why Rendering Is Split Between `GpuDevice` And `Renderer`

- `GpuDevice` owns low-level memory and frame synchronization.
- `Renderer` owns draw semantics, batching, texture binding, and effect orchestration.
- This keeps the backend boundary clear and makes the SDL2 path possible without changing menu widgets.

### `nxui` Resource And Effect Layer

- [lib/nxui/include/nxui/core/Texture.hpp](lib/nxui/include/nxui/core/Texture.hpp) owns image upload and descriptor registration.
- [lib/nxui/include/nxui/core/Font.hpp](lib/nxui/include/nxui/core/Font.hpp) uses SDL_ttf and caches rendered strings as textures with an LRU cache.
- [lib/nxui/include/nxui/core/Animation.hpp](lib/nxui/include/nxui/core/Animation.hpp) provides tweens and an `AnimationManager` singleton for frame-updated animated properties.
- [lib/nxui/include/nxui/core/I18n.hpp](lib/nxui/include/nxui/core/I18n.hpp) loads translation maps and broadcasts language-change callbacks.
- [lib/nxui/include/nxui/core/ThreadPool.hpp](lib/nxui/include/nxui/core/ThreadPool.hpp) provides a small reusable worker pool for background tasks.
- [lib/nxui/include/nxui/widgets/GlassPanel.hpp](lib/nxui/include/nxui/widgets/GlassPanel.hpp) and [lib/nxui/include/nxui/widgets/GlassWidget.hpp](lib/nxui/include/nxui/widgets/GlassWidget.hpp) package blur and liquid-glass rendering into reusable panel widgets.

### Why `nxui` Owns So Much

- SwitchU needs a framework that understands GPU-backed textures, post-processing, controller focus, and touch from the start.
- Desktop UI toolkits would not map cleanly to deko3d, applet lifecycle rules, or Switch-style interaction.
- Keeping animation, i18n, font caching, and background work inside the framework simplifies feature screens and keeps widget code consistent.

## Architecture Rules To Preserve When Editing

- If the change is about launching, suspend, resume, wake, or applet ownership, start in the daemon or the common protocol, not in a menu widget.
- If the change is about layout, overlay behavior, focus, sidebar actions, or touch rules, start in menu code or `nxui` focus or widget code.
- If the change is about blur, glass, offscreen capture, or shader state, start in `nxui::Renderer`, `GpuDevice`, or the glass widgets before changing feature screens.
- If the change crosses the daemon-menu boundary, make the protocol explicit and keep payloads POD-style.
- Keep `AppletStorage` handles closed on all paths.
- Keep refresh work coalesced. Do not reintroduce duplicate refresh passes or hot-path allocator churn.
- Keep daemon code compatible with `-fno-rtti`.
- Do not assume SDL2 parity when you only changed deko3d code.

## Useful References

- [README.md](README.md)
- [xmake.lua](xmake.lua)
- [toolchain/devkitA64.lua](toolchain/devkitA64.lua)
- [toolchain/switch.lua](toolchain/switch.lua)
- [projects/daemon/daemon.json](projects/daemon/daemon.json)
- [projects/menu/menu.json](projects/menu/menu.json)
- [projects/menu/src/core/WiiUMenuApp.hpp](projects/menu/src/core/WiiUMenuApp.hpp)
- [projects/menu/src/core/WiiUMenuApp.cpp](projects/menu/src/core/WiiUMenuApp.cpp)
- [projects/menu/src/core/WiiUMenuAppInteraction.cpp](projects/menu/src/core/WiiUMenuAppInteraction.cpp)
- [projects/common/include/switchu/smi_protocol.hpp](projects/common/include/switchu/smi_protocol.hpp)
- [projects/common/include/switchu/smi_helpers.hpp](projects/common/include/switchu/smi_helpers.hpp)
- [lib/nxui/include/nxui/Application.hpp](lib/nxui/include/nxui/Application.hpp)
- [lib/nxui/include/nxui/Activity.hpp](lib/nxui/include/nxui/Activity.hpp)
- [lib/nxui/include/nxui/widgets/Widget.hpp](lib/nxui/include/nxui/widgets/Widget.hpp)
- [lib/nxui/include/nxui/widgets/Box.hpp](lib/nxui/include/nxui/widgets/Box.hpp)
- [lib/nxui/include/nxui/core/Renderer.hpp](lib/nxui/include/nxui/core/Renderer.hpp)
- [lib/nxui/include/nxui/core/GpuDevice.hpp](lib/nxui/include/nxui/core/GpuDevice.hpp)
- [lib/libnxtc/README.md](lib/libnxtc/README.md)
- [ns_debug.sh](ns_debug.sh)
- [SwitchU-Themes/README.md](SwitchU-Themes/README.md)