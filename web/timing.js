export const DISPLAY_REDRAW_MS = 50;
export const NORMAL_INSTRUCTION_MS = 100;
export const FAST_INSTRUCTION_BUDGET = 20000;

/*
 * Browser equivalent of the LCD ready/busy handshake. Normal execution can
 * advance only after the displayed frame is ready, and no more than ten
 * instructions per second. Fast mode deliberately bypasses both limits.
 */
export class TrainerDisplayPacer {
  displayReadyAt = 0;
  nextInstructionAt = 0;

  frameStarted(now) {
    this.displayReadyAt = Math.max(this.displayReadyAt, now + DISPLAY_REDRAW_MS);
  }

  instructionBudget(now, fastMode) {
    if (fastMode) return FAST_INSTRUCTION_BUDGET;
    if (now < this.displayReadyAt || now < this.nextInstructionAt) return 0;
    this.nextInstructionAt = now + NORMAL_INSTRUCTION_MS;
    return 1;
  }

  reset(now) {
    this.displayReadyAt = now;
    this.nextInstructionAt = now;
  }
}
