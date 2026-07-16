import assert from "node:assert/strict";
import {
  DISPLAY_REDRAW_MS,
  FAST_INSTRUCTION_BUDGET,
  NORMAL_INSTRUCTION_MS,
  TrainerDisplayPacer,
} from "../timing.js";

const pacer = new TrainerDisplayPacer();
pacer.reset(0);
pacer.frameStarted(0);

assert.equal(DISPLAY_REDRAW_MS, 50);
assert.equal(NORMAL_INSTRUCTION_MS, 100);
assert.equal(pacer.instructionBudget(49, false), 0);
assert.equal(pacer.instructionBudget(50, false), 1);

pacer.frameStarted(50);
assert.equal(pacer.instructionBudget(99, false), 0);
assert.equal(pacer.instructionBudget(100, false), 0);
assert.equal(pacer.instructionBudget(149, false), 0);
assert.equal(pacer.instructionBudget(150, false), 1);

pacer.frameStarted(150);
assert.equal(pacer.instructionBudget(151, true), FAST_INSTRUCTION_BUDGET);

console.log("m4f: browser display pacing tests passed");
