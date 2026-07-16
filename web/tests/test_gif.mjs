import assert from "node:assert/strict";
import { GifRecorder } from "../gif.js";

function readWord(bytes, offset) {
  return bytes[offset] | (bytes[offset + 1] << 8);
}

function imageData(bytes) {
  const packed = bytes[10];
  let offset = 13;
  if (packed & 0x80) offset += 3 * (2 << (packed & 7));

  while (offset < bytes.length) {
    const introducer = bytes[offset++];
    if (introducer === 0x21) {
      offset += 1;
      while (true) {
        const length = bytes[offset++];
        if (length === 0) break;
        offset += length;
      }
      continue;
    }
    assert.equal(introducer, 0x2c, "expected an image descriptor");
    const width = readWord(bytes, offset + 4);
    const height = readWord(bytes, offset + 6);
    const imagePacked = bytes[offset + 8];
    offset += 9;
    if (imagePacked & 0x80) offset += 3 * (2 << (imagePacked & 7));
    const minimumCodeSize = bytes[offset++];
    const blocks = [];
    while (true) {
      const length = bytes[offset++];
      if (length === 0) break;
      blocks.push(...bytes.subarray(offset, offset + length));
      offset += length;
    }
    return { width, height, minimumCodeSize, blocks: new Uint8Array(blocks) };
  }
  throw new Error("GIF has no image");
}

function decodeLzw(data, minimumCodeSize) {
  const clear = 1 << minimumCodeSize;
  const end = clear + 1;
  let dictionary;
  let nextCode;
  let codeSize;
  let bitOffset = 0;
  let previous = null;
  const output = [];

  const reset = () => {
    dictionary = new Array(4096);
    for (let code = 0; code < clear; code += 1) dictionary[code] = [code];
    nextCode = end + 1;
    codeSize = minimumCodeSize + 1;
    previous = null;
  };
  const readCode = () => {
    let code = 0;
    for (let bit = 0; bit < codeSize; bit += 1) {
      const source = bitOffset + bit;
      code |= ((data[source >>> 3] >>> (source & 7)) & 1) << bit;
    }
    bitOffset += codeSize;
    return code;
  };

  reset();
  while (bitOffset + codeSize <= data.length * 8) {
    const code = readCode();
    if (code === clear) {
      reset();
      continue;
    }
    if (code === end) break;

    let entry = dictionary[code];
    if (!entry && code === nextCode && previous) entry = [...previous, previous[0]];
    assert.ok(entry, `invalid LZW code ${code}`);
    output.push(...entry);

    if (previous && nextCode < 4096) {
      dictionary[nextCode++] = [...previous, entry[0]];
      if (nextCode === (1 << codeSize) && codeSize < 12) codeSize += 1;
    }
    previous = entry;
  }
  return new Uint8Array(output);
}

const width = 80;
const height = 64;
const rgba = new Uint8ClampedArray(width * height * 4);
const expected = new Uint8Array(width * height);
for (let pixel = 0; pixel < width * height; pixel += 1) {
  const red = (pixel * 17 + 31) & 0xff;
  const green = (pixel * 29 + 47) & 0xff;
  const blue = (pixel * 43 + 59) & 0xff;
  rgba.set([red, green, blue, 255], pixel * 4);
  expected[pixel] = (red & 0xe0) | ((green >>> 3) & 0x1c) | (blue >>> 6);
}

const recorder = new GifRecorder(width, height);
recorder.addFrame(rgba);
recorder.addFrame(new Uint8ClampedArray(rgba.length), 5);
const gif = recorder.encode();
const image = imageData(gif);

assert.equal(new TextDecoder().decode(gif.subarray(0, 6)), "GIF89a");
assert.equal(gif[gif.length - 1], 0x3b);
assert.equal(image.width, width);
assert.equal(image.height, height);
assert.deepEqual(decodeLzw(image.blocks, image.minimumCodeSize), expected);
console.log("m4f: browser GIF encoder tests passed");
