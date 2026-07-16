#!/usr/bin/env python3
"""Single-step an M4F test program in this core and QEMU, comparing state."""

from __future__ import annotations

import argparse
import os
import socket
import subprocess
import sys
import time
from dataclasses import dataclass

PROGRAM_BASE = 0x20000000
PROGRAM_MEMORY_SIZE = 0x10000
PROGRAM_STOP = PROGRAM_BASE + 0xFFF0
CPACR = 0xE000ED88
FPCCR = 0xE000EF34
BOOTSTRAP_ADDRESS = PROGRAM_BASE + 0xFF80
BOOTSTRAP_STEPS = 8
XPSR_COMPARE_MASK = 0xFF0FFC00
FPSCR_COMPARE_MASK = 0xF7C0009F


def u32(data: bytes, offset: int) -> int:
    return int.from_bytes(data[offset : offset + 4], "little")


def fnv1a(data: bytes) -> int:
    value = 0xCBF29CE484222325
    for byte in data:
        value ^= byte
        value = (value * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return value


@dataclass
class State:
    step: int
    address: int
    instruction: int
    size: int
    writes: int
    registers: list[int]
    xpsr: int
    fpscr: int
    singles: list[int]
    memory_hash: int


def parse_state(line: str) -> State:
    fields = line.split()
    if fields[0] not in ("INIT", "TRACE") or len(fields) != 57:
        raise ValueError(f"malformed native trace line: {line}")
    return State(
        step=int(fields[1]),
        address=int(fields[2], 16),
        instruction=int(fields[3], 16),
        size=int(fields[4]),
        writes=int(fields[5]),
        registers=[int(value, 16) for value in fields[6:22]],
        xpsr=int(fields[22], 16),
        fpscr=int(fields[23], 16),
        singles=[int(value, 16) for value in fields[24:56]],
        memory_hash=int(fields[56], 16),
    )


class Remote:
    def __init__(self, sock: socket.socket):
        self.sock = sock

    def _byte(self) -> bytes:
        value = self.sock.recv(1)
        if not value:
            raise RuntimeError("QEMU closed the GDB connection")
        return value

    def _exact(self, count: int) -> bytes:
        value = bytearray()
        while len(value) < count:
            chunk = self.sock.recv(count - len(value))
            if not chunk:
                raise RuntimeError("QEMU closed the GDB connection")
            value.extend(chunk)
        return bytes(value)

    def _packet(self) -> str:
        while self._byte() != b"$":
            pass
        payload = bytearray()
        checksum = 0
        while True:
            byte = self._byte()
            if byte == b"#":
                break
            checksum = (checksum + byte[0]) & 0xFF
            if byte == b"}":
                escaped = self._byte()
                checksum = (checksum + escaped[0]) & 0xFF
                byte = bytes((escaped[0] ^ 0x20,))
            payload.extend(byte)
        received = int(self._exact(2), 16)
        if received != checksum:
            self.sock.sendall(b"-")
            raise RuntimeError("bad checksum from QEMU GDB server")
        self.sock.sendall(b"+")
        return payload.decode("ascii")

    def command(self, payload: str) -> str:
        encoded = payload.encode("ascii")
        checksum = sum(encoded) & 0xFF
        packet = b"$" + encoded + b"#" + f"{checksum:02x}".encode("ascii")
        self.sock.sendall(packet)
        if self._byte() != b"+":
            raise RuntimeError(f"QEMU rejected GDB packet {payload[:32]}")
        return self._packet()

    def write_memory(self, address: int, data: bytes) -> None:
        for offset in range(0, len(data), 512):
            chunk = data[offset : offset + 512]
            response = self.command(
                f"M{address + offset:x},{len(chunk):x}:{chunk.hex()}"
            )
            if response != "OK":
                raise RuntimeError(f"QEMU memory write failed: {response}")

    def read_memory(self, address: int, size: int) -> bytes:
        result = bytearray()
        for offset in range(0, size, 1024):
            length = min(1024, size - offset)
            response = self.command(f"m{address + offset:x},{length:x}")
            if response.startswith("E"):
                raise RuntimeError(f"QEMU memory read failed: {response}")
            result.extend(bytes.fromhex(response))
        return bytes(result)

    def write_register(self, number: int, value: int, size: int = 4) -> None:
        encoded = value.to_bytes(size, "little").hex()
        response = self.command(f"P{number:x}={encoded}")
        if response != "OK":
            raise RuntimeError(f"QEMU register {number} write failed: {response}")

    def read_register(self, number: int, size: int = 4) -> int:
        response = self.command(f"p{number:x}")
        if response.startswith("E") or "x" in response:
            raise RuntimeError(f"QEMU register {number} read failed: {response}")
        raw = bytes.fromhex(response)
        if len(raw) != size:
            raise RuntimeError(
                f"QEMU register {number} has {len(raw)} bytes, expected {size}"
            )
        return int.from_bytes(raw, "little")

    def registers(self) -> tuple[list[int], int, int, list[int]]:
        response = self.command("g")
        if "x" in response:
            raise RuntimeError("QEMU returned unavailable registers")
        raw = bytes.fromhex(response)
        if len(raw) < 68:
            raise RuntimeError(f"short QEMU register packet ({len(raw)} bytes)")
        core = [u32(raw, index * 4) for index in range(16)]
        xpsr = u32(raw, 64)
        singles: list[int] = []
        for index in range(16):
            double = self.read_register(26 + index, 8)
            singles.extend((double & 0xFFFFFFFF, double >> 32))
        fpscr = self.read_register(42)
        return core, xpsr, fpscr, singles


def connect_qemu(qemu: str) -> tuple[subprocess.Popen[bytes], Remote]:
    listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    listener.bind(("127.0.0.1", 0))
    port = listener.getsockname()[1]
    listener.close()
    process = subprocess.Popen(
        [
            qemu,
            "-M",
            "mps2-an386",
            "-cpu",
            "cortex-m4",
            "-S",
            "-gdb",
            f"tcp:127.0.0.1:{port}",
            "-nographic",
        ],
        stdin=subprocess.DEVNULL,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    deadline = time.monotonic() + 5
    while True:
        try:
            sock = socket.create_connection(("127.0.0.1", port), timeout=1)
            sock.settimeout(5)
            remote = Remote(sock)
            remote.command("qSupported:multiprocess+;swbreak+;hwbreak+")
            return process, remote
        except OSError:
            if process.poll() is not None:
                raise RuntimeError("QEMU exited before its GDB server became ready")
            if time.monotonic() >= deadline:
                process.terminate()
                raise RuntimeError("timed out connecting to QEMU GDB server")
            time.sleep(0.05)


def native_trace(runner: str, binary: str) -> tuple[State, list[State], str]:
    completed = subprocess.run(
        [runner, binary, "--trace"],
        check=True,
        stdout=subprocess.PIPE,
        text=True,
    )
    lines = completed.stdout.splitlines()
    initial = parse_state(lines[0])
    traces = [parse_state(line) for line in lines[1:] if line.startswith("TRACE ")]
    result = next(line for line in lines if line.startswith("RESULT "))
    return initial, traces, result


def initialize_qemu(
    remote: Remote, program: bytes, bootstrap: bytes, initial: State
) -> None:
    memory = bytearray(PROGRAM_MEMORY_SIZE)
    memory[: len(program)] = program
    remote.write_memory(PROGRAM_BASE, memory)
    remote.write_memory(BOOTSTRAP_ADDRESS, bootstrap)
    remote.write_register(25, 0x01000000)
    remote.write_register(15, BOOTSTRAP_ADDRESS)
    for _ in range(BOOTSTRAP_STEPS):
        response = remote.command("s")
        if not response.startswith(("S", "T")):
            raise RuntimeError(f"QEMU bootstrap step failed: {response}")
    if remote.read_memory(CPACR, 4) != (0x00F00000).to_bytes(4, "little"):
        raise RuntimeError("QEMU bootstrap did not enable CP10/CP11")
    if remote.read_memory(FPCCR, 4) != bytes(4):
        raise RuntimeError("QEMU bootstrap did not disable automatic FP stacking")
    remote.write_memory(PROGRAM_BASE, memory)
    remote.write_register(25, initial.xpsr)
    remote.write_register(46, 0)  # CONTROL
    remote.write_register(43, initial.registers[13])  # MSP
    for index, value in enumerate(initial.registers):
        remote.write_register(index, value)
    for index in range(16):
        low = initial.singles[index * 2]
        high = initial.singles[index * 2 + 1]
        remote.write_register(26 + index, low | (high << 32), 8)
    remote.write_register(42, initial.fpscr)
    actual_hash = fnv1a(remote.read_memory(PROGRAM_BASE, PROGRAM_MEMORY_SIZE))
    if actual_hash != initial.memory_hash:
        raise RuntimeError(
            f"initial memory differs: m4f={initial.memory_hash:016x} "
            f"qemu={actual_hash:016x}"
        )


def mismatch(state: State, field: str, expected: int, actual: int) -> RuntimeError:
    return RuntimeError(
        f"step {state.step} at {state.address:08x} "
        f"instruction {state.instruction:0{state.size * 2}x}: {field} differs "
        f"(m4f={expected:08x}, qemu={actual:08x})"
    )


def compare_state(remote: Remote, state: State) -> None:
    core, xpsr, fpscr, singles = remote.registers()
    for index, (expected, actual) in enumerate(zip(state.registers, core)):
        if expected != actual:
            raise mismatch(state, f"r{index}", expected, actual)
    expected_xpsr = state.xpsr & XPSR_COMPARE_MASK
    actual_xpsr = xpsr & XPSR_COMPARE_MASK
    if expected_xpsr != actual_xpsr:
        raise mismatch(state, "xpsr", expected_xpsr, actual_xpsr)
    expected_fpscr = state.fpscr & FPSCR_COMPARE_MASK
    actual_fpscr = fpscr & FPSCR_COMPARE_MASK
    if expected_fpscr != actual_fpscr:
        raise mismatch(state, "fpscr", expected_fpscr, actual_fpscr)
    for index, (expected, actual) in enumerate(zip(state.singles, singles)):
        if expected != actual:
            raise mismatch(state, f"s{index}", expected, actual)
    if state.writes or state.registers[15] == PROGRAM_STOP:
        actual_hash = fnv1a(remote.read_memory(PROGRAM_BASE, PROGRAM_MEMORY_SIZE))
        if state.memory_hash != actual_hash:
            raise RuntimeError(
                f"step {state.step} at {state.address:08x}: memory differs "
                f"(m4f={state.memory_hash:016x}, qemu={actual_hash:016x})"
            )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--runner", required=True)
    parser.add_argument("--qemu", default="qemu-system-arm")
    parser.add_argument("--bootstrap", required=True)
    parser.add_argument("program", nargs="+")
    arguments = parser.parse_args()

    with open(arguments.bootstrap, "rb") as file:
        bootstrap = file.read()
    for binary in arguments.program:
        initial, traces, result = native_trace(arguments.runner, binary)
        with open(binary, "rb") as file:
            program = file.read()
        process: subprocess.Popen[bytes] | None = None
        remote: Remote | None = None
        try:
            process, remote = connect_qemu(arguments.qemu)
            initialize_qemu(remote, program, bootstrap, initial)
            for state in traces:
                response = remote.command("s")
                if not response.startswith(("S", "T")):
                    raise RuntimeError(f"unexpected QEMU step response: {response}")
                compare_state(remote, state)
            print(
                f"m4f: QEMU differential {os.path.basename(binary)} passed "
                f"({len(traces)} instructions; {result})"
            )
        finally:
            if remote is not None:
                remote.sock.close()
            if process is not None:
                process.terminate()
                try:
                    process.wait(timeout=2)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError, subprocess.CalledProcessError, ValueError) as error:
        print(f"qemu_diff.py: {error}", file=sys.stderr)
        sys.exit(1)
