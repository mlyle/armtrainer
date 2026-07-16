# M4F emulator core

`m4femu` is an allocation-free C implementation of the Cortex-M4F execution
architecture. It is intentionally isolated from the trainer firmware and from
STM32 peripheral definitions so the same core can be linked into a native test
program or compiled to WebAssembly.

The public API is `include/m4f/m4f.h`. A caller supplies little-endian memory
callbacks, initializes `struct m4f_cpu`, and calls `m4f_step()` to execute at
most one instruction. There is no global state, allocation, firmware include,
or platform dependency beyond the C library and `libm`.

The implementation covers the Thumb/Thumb-2, ARMv7E-M DSP, and
FPv4-SP-D16 instruction families implemented by Cortex-M4F. This includes
integer and DSP arithmetic, branches and IT blocks, all scalar load/store
widths and addressing forms, multiple/dual/exclusive transfers, system
register and barrier instructions, floating-point arithmetic/conversion, and
the raw S/D register transfer forms supported by FPv4-SP-D16.

SVC, BKPT, WFI/WFE, UDF, invalid encodings, and memory faults are returned to
the embedding application as stop reasons. They do not enter architectural
exceptions. Exception return is consequently outside this core as well.

The core does not model an STM32 bus, MPU, clock tree, GPIO, flash controller,
or any other device peripheral. The embedding application decides which
addresses exist and how accesses behave. Ordinary and explicitly unprivileged
memory instructions reach the same callbacks; an embedding that needs MPU or
privilege enforcement must apply it around those callbacks. Floating-point
exception status is accumulated in FPSCR, but no FP exception trap is entered.

`r[15]` is the next even instruction address and `r[13]` is the active stack
pointer. `msp` and `psp` are synchronized with `r[13]` at step boundaries.
S0-S31 are stored as raw IEEE-754 bit patterns, which also provides the D0-D15
view used by load/store and core-register transfers.

Minimal use looks like:

```c
struct m4f_cpu cpu;
struct m4f_memory memory = { read, write, context };

m4f_reset(&cpu, &memory);
cpu.r[13] = initial_sp;
m4f_set_pc(&cpu, entry);

for (;;) {
    struct m4f_step_result step = m4f_step(&cpu);
    if (step.stop != M4F_STEP_OK)
        break;
}
```

Run the native vector tests from the repository root with:

```
make m4f-test
```

The test target runs semantic vectors and a complete small student program,
then assembles representative forms of every M4F mnemonic family with the GNU
Arm assembler and checks that the emulator decodes them. Additional build
checks are:

```
make m4f-sanitize
make m4f-wasm-check
make m4f-wasm-test
make m4f-program-test
make m4f-qemu-test
```

`m4f-wasm-check` produces `build/m4femu/wasm/libm4f.a` with Emscripten.
`m4f-wasm-test` links the same C vector runner to WebAssembly and executes it
under Node.

## Compiled-program and QEMU benches

`m4f-program-test` compiles the sources in `tests/programs/` with
`arm-none-eabi-gcc` as freestanding Cortex-M4F code. Each ELF enters directly
at the ordinary C function `student_main`: it has no vector table, reset
handler, CRT startup, libc, or exception handler. The bench initializes the
argument registers, SP, LR, and PC, runs single steps until the function
returns through LR, and compares its return value and output buffer with a
native build of the same defined C code.

`m4f-qemu-test` runs those same raw binaries against QEMU's Cortex-M4 model.
After every instruction it compares R0-R15, architectural xPSR flags and IT
state, S0-S31, and FPSCR. It compares the full 64 KiB test RAM image after
store instructions and again on return. A small QEMU-only bootstrap enables
CP10/CP11 and disables automatic FP stacking before all observable state is
reset; it is not linked into the student programs. Any mismatch reports the
step number, address, instruction word, register or memory field, and both
values.
