function littleEndian(value) {
  return [value & 0xff, (value >>> 8) & 0xff];
}

function palette332() {
  const palette = [];
  for (let index = 0; index < 256; index += 1) {
    const red = index >>> 5;
    const green = (index >>> 2) & 7;
    const blue = index & 3;
    palette.push(
      Math.round(red * 255 / 7),
      Math.round(green * 255 / 7),
      Math.round(blue * 255 / 3),
    );
  }
  return palette;
}

function indexed332(rgba) {
  const indexed = new Uint8Array(rgba.length / 4);
  for (let source = 0, destination = 0; source < rgba.length; source += 4) {
    indexed[destination] = (rgba[source] & 0xe0)
      | ((rgba[source + 1] >>> 3) & 0x1c)
      | (rgba[source + 2] >>> 6);
    destination += 1;
  }
  return indexed;
}

function lzwEncode(indexed) {
  const clear = 256;
  const end = 257;
  let codeSize = 9;
  let nextCode = 258;
  let dictionary = new Map();
  const output = [];
  let bits = 0;
  let bitCount = 0;

  const write = (code) => {
    bits |= code << bitCount;
    bitCount += codeSize;
    while (bitCount >= 8) {
      output.push(bits & 0xff);
      bits >>>= 8;
      bitCount -= 8;
    }
  };

  const reset = () => {
    dictionary = new Map();
    codeSize = 9;
    nextCode = 258;
  };

  write(clear);
  if (indexed.length) {
    let prefix = indexed[0];
    for (let offset = 1; offset < indexed.length; offset += 1) {
      const suffix = indexed[offset];
      const key = `${prefix},${suffix}`;
      const found = dictionary.get(key);
      if (found !== undefined) {
        prefix = found;
        continue;
      }

      write(prefix);
      if (nextCode < 4096) {
        dictionary.set(key, nextCode);
        nextCode += 1;
        /*
         * The decoder creates an entry after reading a code, so its table is
         * one entry behind the encoder.  GIF therefore changes widths one
         * entry later than the tempting nextCode === 2^codeSize test.
         */
        if (nextCode > (1 << codeSize) && codeSize < 12) codeSize += 1;
      } else {
        write(clear);
        reset();
      }
      prefix = suffix;
    }
    write(prefix);
  }
  write(end);
  if (bitCount) output.push(bits & 0xff);
  return new Uint8Array(output);
}

function dataBlocks(bytes) {
  const blockCount = Math.ceil(bytes.length / 255);
  const blocks = new Uint8Array(bytes.length + blockCount + 1);
  let destination = 0;
  for (let source = 0; source < bytes.length; source += 255) {
    const length = Math.min(255, bytes.length - source);
    blocks[destination++] = length;
    blocks.set(bytes.subarray(source, source + length), destination);
    destination += length;
  }
  blocks[destination] = 0;
  return blocks;
}

export class GifRecorder {
  constructor(width, height) {
    this.width = width;
    this.height = height;
    this.frames = [];
  }

  addFrame(rgba, delayCentiseconds = 10) {
    if (rgba.length !== this.width * this.height * 4) {
      throw new Error("GIF frame has the wrong dimensions");
    }
    this.frames.push({
      data: lzwEncode(indexed332(rgba)),
      delay: Math.max(1, Math.min(65535, Math.round(delayCentiseconds))),
    });
  }

  encode() {
    if (!this.frames.length) throw new Error("GIF has no frames");
    const chunks = [new Uint8Array([
      ...new TextEncoder().encode("GIF89a"),
      ...littleEndian(this.width),
      ...littleEndian(this.height),
      0xf7, 0, 0,
      ...palette332(),
      0x21, 0xff, 0x0b,
      ...new TextEncoder().encode("NETSCAPE2.0"),
      0x03, 0x01, 0x00, 0x00, 0x00,
    ])];

    for (const frame of this.frames) {
      chunks.push(new Uint8Array([
        0x21, 0xf9, 0x04, 0x00,
        ...littleEndian(frame.delay),
        0x00, 0x00,
        0x2c,
        0x00, 0x00, 0x00, 0x00,
        ...littleEndian(this.width),
        ...littleEndian(this.height),
        0x00,
        0x08,
      ]));
      chunks.push(dataBlocks(frame.data));
    }
    chunks.push(Uint8Array.of(0x3b));

    const length = chunks.reduce((total, chunk) => total + chunk.length, 0);
    const bytes = new Uint8Array(length);
    let offset = 0;
    for (const chunk of chunks) {
      bytes.set(chunk, offset);
      offset += chunk.length;
    }
    return bytes;
  }
}
