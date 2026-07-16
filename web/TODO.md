# Web Trainer Roadmap

The browser version should preserve the physical trainer's behavior and reuse
the firmware's C implementation wherever possible. Browser-specific code stays
under `web/`; shared interfaces should describe trainer concepts rather than
HTML, JavaScript, or Emscripten concepts. The embedded build must remain a
first-class target and must pass after every phase.

## Completed foundation

Current functionality:

- Run `m4femu` as WebAssembly in a Web Worker.
- Load raw trainer `.bin` programs into virtual RAM.
- Render a 160x128 trainer screen to a browser canvas.
- Accept trainer keys from the computer keyboard.
- Run, stop, single-step, reset, and expose basic status.
- Record an inclusive instruction range to an animated GIF, with one LCD frame
  per instruction and no dependency added to the embedded build.
- Compile the embedded LCD rasterizer, fonts, console, monitor UI and editing
  state machine, keyboard handling, register/disassembly views, and SVC
  dispatcher into both firmware and Wasm builds.
- Keep STM32 exception entry, CoreDebug control, clocks, bootloader transfer,
  SPI/DMA, and GPIO in the default embedded adapters.
- Adapt `m4femu` registers to the same exception-frame view consumed by the
  physical monitor.
- Use narrow platform functions for ARM-addressed memory, LED state, time,
  randomness, save slots, delays, and single-step control.
- Share nonblocking numeric-input parsing with the embedded console while the
  physical `console_read_number()` retains its existing blocking wrapper.
- Model the LCD ready/busy handshake in the web adapter: a redraw takes 50 ms,
  normal mode is limited to ten instructions per second, and SVC `0x40` enables
  fast mode.

Before adding features, keep native, sanitizer, Wasm, browser-module, QEMU, and
embedded builds green.

## Phase 1: Complete behavioral parity

- Convert delay operations into an explicit web wait state; keep the embedded
  delay implementation unchanged.
- Check every documented and currently used student SVC against the physical
  behavior, including blink timing and the hidden snake path.
- Add framebuffer golden tests for the splash, monitor, console, icons, and
  register pages.
- Add shared monitor tests for load/store editing, LOAD combinations,
  save/load slots, branch helper, decimal view, and run/step transitions.
- Decide whether the web save-slot backend should reset with the emulated CPU
  or behave like persistent flash across resets.

Exit criterion: the same SVC test vectors pass against the embedded adapter,
native host harness, and Wasm runtime, and representative framebuffer hashes
match.

## Phase 2: Debugging views

Add browser views without changing the shared trainer core:

- Full register and flag view, including high and floating-point registers.
- Memory inspector/editor with byte, halfword, and word formats.
- Disassembly centered on PC or an entered address.
- Breakpoints, run-to-address, and instruction history.
- Optional execution-speed control while retaining the physical 10 Hz mode as
  the default.

State should be requested from the Worker as snapshots. UI components must not
reach directly into mutable Wasm memory while execution is active.

## Phase 3: Projects, listings, and printing

- Import and export raw `.bin` files.
- Model the trainer's three 16 KiB save slots.
- Store named projects and metadata locally in IndexedDB.
- Turn the disassembler into a reusable C library with structured listing
  output, including icon data.
- Render printable listings as HTML with print CSS and support `.lst` download.
- Add explicit backup/export because browser storage is not guaranteed archival
  storage.

## Phase 4: Full trainer presentation

- Add a responsive image or illustration of the complete trainer.
- Overlay the real 160x128 canvas at the display location.
- Add clickable and touch-friendly keys while retaining all keyboard controls.
- Show LED and other visible board state.
- Preserve accessible HTML controls and focus behavior rather than making the
  trainer one opaque canvas.

## Phase 5: Real hardware transport

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
- Framebuffer golden/parity tests for shared LCD and monitor behavior.
- Manual browser checks for keyboard focus, run/stop responsiveness, display
  pacing, file loading, and mobile layout.
