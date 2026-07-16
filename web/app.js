const canvas = document.querySelector("#screen");
const context = canvas.getContext("2d", { alpha: false });
const stateElement = document.querySelector("#state");
const machineElement = document.querySelector("#machine");
const ledElement = document.querySelector("#led");
const fileInput = document.querySelector("#program");
const worker = new Worker(new URL("./worker.js", import.meta.url), { type: "module" });

let ready = false;

const stateNames = ["Stopped", "Running", "Waiting for input", "Fault"];
const stopNames = [
  "ok", "svc", "breakpoint", "wait for interrupt", "wait for event", "undefined",
  "undefined instruction", "unpredictable instruction", "fetch fault", "data fault",
  "alignment fault",
];

function hex(value) {
  return (value >>> 0).toString(16).padStart(8, "0").toUpperCase();
}

function updateStatus(message) {
  const state = stateNames[message.state] ?? "Unknown";
  stateElement.textContent = message.state === 3
    ? `${state}: ${stopNames[message.stop] ?? "emulator stopped"}`
    : `${state}${message.fast ? " · Fast" : ""}`;
  machineElement.textContent = `PC ${hex(message.pc)} · ${message.instructions.toLocaleString()} insns`;
  ledElement.classList.toggle("on", Boolean(message.led));
}

worker.addEventListener("message", (event) => {
  const message = event.data;
  if (message.type === "ready") {
    ready = true;
    stateElement.textContent = "Stopped";
    loadBundledExample();
  } else if (message.type === "frame") {
    context.putImageData(new ImageData(new Uint8ClampedArray(message.pixels), 160, 128), 0, 0);
    updateStatus(message);
  } else if (message.type === "error") {
    stateElement.textContent = message.message;
  }
});

async function loadProgram(buffer, name) {
  if (!ready) return;
  worker.postMessage({ type: "load", buffer, name }, [buffer]);
}

async function loadBundledExample() {
  try {
    const response = await fetch(new URL("../examples/erik-showicon.bin", window.location.href));
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    await loadProgram(await response.arrayBuffer(), "erik-showicon.bin");
  } catch (error) {
    stateElement.textContent = "Ready — choose a .bin program";
    console.warn("Could not load bundled example", error);
  }
}

fileInput.addEventListener("change", async () => {
  const [file] = fileInput.files;
  if (file) await loadProgram(await file.arrayBuffer(), file.name);
  fileInput.value = "";
});

document.querySelector("#run").addEventListener("click", () => worker.postMessage({ type: "run" }));
document.querySelector("#step").addEventListener("click", () => worker.postMessage({ type: "step" }));
document.querySelector("#reset").addEventListener("click", () => worker.postMessage({ type: "reset" }));

function trainerKey(event) {
  if (event.metaKey || event.ctrlKey || event.altKey) return null;
  if (/^[0-9a-f]$/i.test(event.key)) return event.key.toLowerCase().charCodeAt(0);
  if (/^[lsg]$/i.test(event.key)) return event.key.toLowerCase().charCodeAt(0);
  if (event.key === "Enter") return 13;
  if (event.key === "Backspace") return 8;
  if (event.key === " " || event.key === ".") return ".".charCodeAt(0);
  return null;
}

window.addEventListener("keydown", (event) => {
  if (event.target === fileInput || event.target instanceof HTMLButtonElement) return;
  if (!event.repeat && event.key.toLowerCase() === "r" && !event.metaKey && !event.ctrlKey) {
    event.preventDefault();
    worker.postMessage({ type: "reset" });
    return;
  }
  const key = trainerKey(event);
  if (key === null) return;
  event.preventDefault();
  worker.postMessage({ type: "key", key, pressed: true });
});

window.addEventListener("keyup", (event) => {
  if (event.target === fileInput || event.target instanceof HTMLButtonElement) return;
  const key = trainerKey(event);
  if (key === null) return;
  event.preventDefault();
  worker.postMessage({ type: "key", key, pressed: false });
});
