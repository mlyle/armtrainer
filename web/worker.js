import createTrainerModule from "../build/web/trainer.mjs";
import { TrainerDisplayPacer } from "./timing.js";

const module = await createTrainerModule();
const frameBytes = 160 * 128 * 4;
const pacer = new TrainerDisplayPacer();
let lastRevision = -1;

function snapshot(force = false, now = performance.now()) {
  const revision = module._trainer_frame_revision();
  if (!force && revision === lastRevision) return;
  const changed = revision !== lastRevision;
  lastRevision = revision;
  const pointer = module._trainer_framebuffer();
  const pixels = module.HEAPU8.slice(pointer, pointer + frameBytes).buffer;
  postMessage({
    type: "frame",
    pixels,
    state: module._trainer_state(),
    pc: module._trainer_pc(),
    stop: module._trainer_last_stop(),
    led: module._trainer_led(),
    fast: module._trainer_fast_mode(),
    instructions: module._trainer_instruction_count(),
  }, [pixels]);
  if (changed) pacer.frameStarted(now);
}

module._trainer_init();
pacer.reset(performance.now());
postMessage({ type: "ready" });
snapshot(true);

self.addEventListener("message", (event) => {
  const message = event.data;
  if (message.type === "load") {
    const bytes = new Uint8Array(message.buffer);
    if (bytes.length > module._trainer_program_capacity()) {
      postMessage({ type: "error", message: `${message.name} is too large for trainer memory` });
      return;
    }
    module.HEAPU8.set(bytes, module._trainer_program_buffer());
    if (!module._trainer_load(bytes.length)) {
      postMessage({ type: "error", message: `Could not load ${message.name}` });
      return;
    }
  } else if (message.type === "reset") {
    module._trainer_reset();
  } else if (message.type === "run") {
    module._trainer_command_run();
  } else if (message.type === "step") {
    module._trainer_command_step();
  } else if (message.type === "key") {
    module._trainer_key(message.key, message.pressed ? 1 : 0);
  }
  snapshot(true);
});

setInterval(() => {
  const now = performance.now();
  const budget = pacer.instructionBudget(now, module._trainer_fast_mode());
  if (!budget) return;
  module._trainer_run_slice(budget, now >>> 0);
  snapshot(false, performance.now());
}, 10);
