# Web Trainer Roadmap

The browser version should preserve the physical trainer's behavior and reuse
the firmware's C implementation wherever possible. Browser-specific code stays
under `web/`; shared interfaces should describe trainer concepts rather than
HTML, JavaScript, or Emscripten concepts. The embedded build must remain a
first-class target and must pass after every phase.

## Phase 0: MVP baseline

Current functionality:

- Run `m4femu` as WebAssembly in a Web Worker.
- Load raw trainer `.bin` programs into virtual RAM.
- Render a 160x128 trainer screen to a browser canvas.
- Accept trainer keys from the computer keyboard.
- Run, stop, single-step, reset, and expose basic status.
- Handle the commonly used trainer SVC calls for display, console, input,
  random numbers, time, colors, and LED state.
- Model the LCD ready/busy handshake in the web adapter: a redraw takes 50 ms,
  normal mode is limited to ten instructions per second, and SVC `0x40` enables
  fast mode.

Before adding features, keep native, sanitizer, Wasm, browser-module, QEMU, and
embedded builds green.

## Phase 1: Share the LCD and console implementation

The MVP duplicates a small framebuffer renderer so it can run without STM32
drivers. Replace that duplication with the firmware implementation:

- Split `src/lcd.c` into platform-neutral framebuffer/raster operations and an
  STM32 SPI/DMA transport backend.
- Keep the existing `lcd_*` API used by `src/console.c` and the monitor.
- Add a web LCD backend implementing `lcd_init`, `lcd_refresh`,
  `lcd_is_ready`, and `lcd_release` without including browser concepts in the
  shared code.
- Make `lcd_refresh` publish a framebuffer generation; have the web backend
  remain busy for 50 ms before `lcd_is_ready` becomes true.
- Compile the real fonts, LCD drawing code, and `src/console.c` into both the
  embedded firmware and the browser Wasm module.
- Add framebuffer parity tests so identical C calls produce identical pixels
  in native and Wasm builds.

Exit criterion: the web-specific C code no longer contains font rasterization,
console scrolling, or pixel drawing implementations.

## Phase 2: Extract a shared monitor core

Separate monitor behavior from Cortex-M exception entry and physical address
dereferences:

- Define a platform-neutral CPU view containing R0-R15, xPSR, floating-point
  state, and run state.
- Define narrow memory read/write callbacks using ARM addresses.
- Move address/value editing, cursor movement, register rendering, disassembly
  rendering, keyboard handling, and run/step state into a shared monitor core.
- Keep `DebugMon_Handler`, `SVCall_Handler`, `CoreDebug` register access, clock
  setup, and `code_invoke` in the embedded adapter.
- Add an `m4femu` adapter that presents `struct m4f_cpu` through the same CPU
  view and memory callbacks.
- Do not add `#ifdef __EMSCRIPTEN__` branches to ordinary firmware logic.

Exit criterion: the physical trainer and browser call the same monitor key and
screen-rendering functions, with only exception/execution mechanics differing.

## Phase 3: Share SVC services and asynchronous waits

- Move the SVC switch into a shared service dispatcher operating on the common
  CPU view.
- Provide platform backends for LED, monotonic time, random seed input, and
  program memory.
- Convert delay and numeric-input operations into explicit wait states so the
  Worker remains responsive without Asyncify or blocking JavaScript.
- Preserve physical behavior: normal execution waits for LCD readiness; fast
  mode may continue while a redraw is in progress.
- Implement and test every documented and currently used student SVC.

Exit criterion: the same SVC test vectors pass against the embedded adapter,
native host harness, and Wasm runtime.

## Phase 4: Debugging views

Add browser views without changing the shared trainer core:

- Full register and flag view, including high and floating-point registers.
- Memory inspector/editor with byte, halfword, and word formats.
- Disassembly centered on PC or an entered address.
- Breakpoints, run-to-address, and instruction history.
- Optional execution-speed control while retaining the physical 10 Hz mode as
  the default.

State should be requested from the Worker as snapshots. UI components must not
reach directly into mutable Wasm memory while execution is active.

## Phase 5: Projects, listings, and printing

- Import and export raw `.bin` files.
- Model the trainer's three 16 KiB save slots.
- Store named projects and metadata locally in IndexedDB.
- Turn the disassembler into a reusable C library with structured listing
  output, including icon data.
- Render printable listings as HTML with print CSS and support `.lst` download.
- Add explicit backup/export because browser storage is not guaranteed archival
  storage.

## Phase 6: Full trainer presentation

- Add a responsive image or illustration of the complete trainer.
- Overlay the real 160x128 canvas at the display location.
- Add clickable and touch-friendly keys while retaining all keyboard controls.
- Show LED and other visible board state.
- Preserve accessible HTML controls and focus behavior rather than making the
  trainer one opaque canvas.

## Phase 7: Real hardware transport

- Add WebUSB as an optional JavaScript transport, not as a dependency of the C
  trainer core.
- Implement the STM32 ROM DFU upload/download operations needed for the three
  save slots.
- Restrict writes to known project sectors and verify every write by reading it
  back.
- Keep file import/export as the portable fallback for browsers without
  WebUSB.
- Only consider firmware flashing after project transfer is reliable and
  separately guarded.

## Continuous verification

Each phase should retain these checks:

- Forced embedded firmware compile and link with `-Werror`.
- Native and sanitizer tests for shared trainer code.
- Wasm tests running the same C test vectors.
- `m4femu` instruction corpus, decoder sweep, compiled-program tests, and QEMU
  differential tests.
- Browser-module test using a real saved classroom program.
- Framebuffer golden/parity tests once the shared LCD extraction is complete.
- Manual browser checks for keyboard focus, run/stop responsiveness, display
  pacing, file loading, and mobile layout.
