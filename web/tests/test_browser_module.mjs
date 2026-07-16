import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import createTrainerModule from "../../build/web/trainer.mjs";

const wasmBinary = await readFile(new URL("../../build/web/trainer.wasm", import.meta.url));
const module = await createTrainerModule({ wasmBinary });

module._trainer_init();
assert.equal(module._trainer_state(), 0);
assert.ok(module.HEAPU8 instanceof Uint8Array);

const example = await readFile(new URL("../../examples/erik-showicon.bin", import.meta.url));
assert.ok(example.length <= module._trainer_program_capacity());
module.HEAPU8.set(example, module._trainer_program_buffer());
assert.equal(module._trainer_load(example.length), 1);

module._trainer_command_run();
assert.equal(module._trainer_state(), 1);
assert.equal(module._trainer_run_slice(500, 1000), 500);
assert.equal(module._trainer_state(), 1);
assert.ok(module._trainer_instruction_count() >= 500);

const frame = module.HEAPU8.subarray(
  module._trainer_framebuffer(),
  module._trainer_framebuffer() + 160 * 128 * 4,
);
let coloredPixels = 0;
for (let offset = 0; offset < frame.length; offset += 4) {
  if (frame[offset] || frame[offset + 1] || frame[offset + 2]) coloredPixels += 1;
}
assert.ok(coloredPixels > 100);

console.log("m4f: browser-target Wasm module tests passed");
