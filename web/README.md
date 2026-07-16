# Browser MVP

The browser build runs student Thumb code through `m4femu` in a Web Worker. It
compiles the firmware's real LCD rasterizer, console, monitor state machine,
keyboard handling, register/disassembly display, save-slot behavior, and SVC
dispatcher into WebAssembly. The code under `web/c/` is limited to the
`m4femu`/exception-frame adapter and platform implementations for memory, LCD
transport, time, LED state, and save storage. STM32 remains the default build
and hardware backend.

Build and test it with:

```sh
make web-test web
```

Serve the repository root (not the `web` directory) so the generated Wasm and
bundled example are both available:

```sh
make web-serve
```

Then open <http://localhost:8000/web/>. The page loads
`examples/erik-showicon.bin` initially, and another raw trainer `.bin` can be
selected from the file control.

Keyboard mapping:

- `0`-`9`, `A`-`F`: enter hexadecimal digits
- `Enter`: switch between address and value entry
- `L`: load
- `S`: store
- `Backspace`: move the edit cursor left
- `G`: run or stop
- `Space` or `.`: single step
- `R`: reset the emulated CPU

Normal run mode waits for the browser display to become ready after each frame.
The display stays busy for 50 ms, and normal execution is capped at ten
instructions per second. Student SVC `0x40` enables fast mode and bypasses this
display pacing; SVC `0x41` restores normal mode and stops.

The **Record GIF** action captures the LCD at every instruction in an inclusive
start/end instruction range, runs the program if necessary, stops at the ending
instruction, and downloads the result. The start must be at or after the
current instruction counter shown below the display; reset the trainer to use a
range beginning at zero. Recordings are limited to 5,001 frames to keep browser
memory use bounded.

The shared dispatcher implements the trainer SVC calls for display, numbers,
random, time, key input, colors, and LED state. Delay calls currently complete
immediately in the web backend. Persistent projects, WebUSB, a photographed
trainer skin, and debugger side panels are intentionally outside this first
slice.

See [TODO.md](TODO.md) for the remaining parity, debugger, project,
trainer-skin, and hardware-transfer phases.
