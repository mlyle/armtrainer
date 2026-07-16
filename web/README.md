# Browser MVP

The browser build runs student Thumb code through `m4femu` in a Web Worker and
renders a C-owned 160x128 trainer framebuffer. Browser-specific JavaScript and
Emscripten exports are contained in this directory; the embedded firmware does
not include web conditionals.

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

The MVP implements the common trainer SVC calls for display, numbers, random,
time, key input, colors, and LED state. Delay calls currently complete
immediately. STM32 peripherals, save slots, WebUSB, a photographed trainer
skin, and debugger side panels are intentionally outside this first slice.

See [TODO.md](TODO.md) for the phased plan to converge the browser build onto
the shared firmware LCD, console, monitor, and SVC implementations before
adding debugger, project, trainer-skin, and hardware-transfer features.
