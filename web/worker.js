import createTrainerModule from "../build/web/trainer.mjs";

const module = await createTrainerModule();
const packedFrameBytes = 160 * 128 * 3 / 2;
let lastRevision = -1;
let recording = null;

function snapshot(force = false) {
  const revision = module._trainer_frame_revision();
  const instructions = module._trainer_instruction_count();
  const recordFrame = recording !== null
    && instructions >= recording.start
    && instructions <= recording.end
    && instructions !== recording.lastFrame;
  if (!force && revision === lastRevision && !recordFrame) return;
  lastRevision = revision;
  const pointer = module._trainer_framebuffer();
  const packed = module.HEAPU8.subarray(pointer, pointer + packedFrameBytes);
  const pixels = new Uint8ClampedArray(160 * 128 * 4);
  for (let source = 0, destination = 0; source < packed.length; source += 3) {
    pixels[destination] = (packed[source] >>> 4) * 17;
    pixels[destination + 1] = (packed[source] & 0x0f) * 17;
    pixels[destination + 2] = (packed[source + 1] >>> 4) * 17;
    pixels[destination + 3] = 255;
    pixels[destination + 4] = (packed[source + 1] & 0x0f) * 17;
    pixels[destination + 5] = (packed[source + 2] >>> 4) * 17;
    pixels[destination + 6] = (packed[source + 2] & 0x0f) * 17;
    pixels[destination + 7] = 255;
    destination += 8;
  }
  const finishesRecording = recordFrame && instructions >= recording.end;
  if (recordFrame) recording.lastFrame = instructions;
  postMessage({
    type: "frame",
    pixels: pixels.buffer,
    state: module._trainer_state(),
    pc: module._trainer_pc(),
    stop: module._trainer_last_stop(),
    led: module._trainer_led(),
    fast: module._trainer_fast_mode(),
    instructions,
    recordFrame,
  }, [pixels.buffer]);

  if (finishesRecording) {
    if (module._trainer_state() === 1) module._trainer_command_run();
    postMessage({ type: "record-complete", start: recording.start, end: recording.end });
    recording = null;
  }
}

module._trainer_init();
postMessage({ type: "ready" });
snapshot(true);

self.addEventListener("message", (event) => {
  const message = event.data;
  if (message.type === "load") {
    if (recording) {
      postMessage({ type: "record-error", message: "Recording canceled by program load" });
      recording = null;
    }
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
    if (recording) {
      postMessage({ type: "record-error", message: "Recording canceled by reset" });
      recording = null;
    }
    module._trainer_reset();
  } else if (message.type === "run") {
    module._trainer_command_run();
  } else if (message.type === "step") {
    module._trainer_command_step();
  } else if (message.type === "key") {
    module._trainer_key(message.key, message.pressed ? 1 : 0);
  } else if (message.type === "record") {
    const current = module._trainer_instruction_count();
    if (!Number.isSafeInteger(message.start) || !Number.isSafeInteger(message.end)
        || message.start < current || message.end < message.start
        || message.end - message.start > 5000 || module._trainer_state() === 3) {
      postMessage({
        type: "record-error",
        message: `Recording must start at or after current instruction ${current}`,
      });
      return;
    }
    recording = { start: message.start, end: message.end, lastFrame: null };
    snapshot(true);
    if (recording && module._trainer_state() === 0) module._trainer_command_run();
    return;
  }
  snapshot(true);
});

setInterval(() => {
  const now = performance.now();
  module._trainer_run_slice(recording ? 1 : 20000, now >>> 0);
  snapshot();
  if (recording && module._trainer_instruction_count() < recording.end
      && (module._trainer_state() === 0 || module._trainer_state() === 3)) {
    postMessage({
      type: "record-error",
      message: module._trainer_state() === 3
        ? "Recording stopped because the program faulted"
        : "Recording stopped before the ending instruction",
    });
    recording = null;
  }
}, 10);
