'use strict';

const fs = require('node:fs');
const output = process.argv[2];
if (!output) throw new Error('Usage: node footsteps.cjs <output-header>');

function synthesize(seed, blocks) {
    const count = blocks * 28;
    function noise() {
        seed ^= seed << 13;
        seed ^= seed >>> 17;
        seed ^= seed << 5;
        return (seed >>> 0) / 2147483648 - 1;
    }

    const crackles = Array.from({ length: 18 }, (_, i) => ({
        start: Math.floor(count * (0.018 + i * 0.043 + noise() * 0.018)),
        length: 12 + Math.floor((noise() + 1) * 23),
        level: 0.14 + (noise() + 1) * 0.17
    }));
    let low = 0;
    let slow = 0;
    let previous = 0;
    const samples = Array.from({ length: count }, (_, i) => {
        const white = noise();
        low += (white - low) * 0.28;
        slow += (low - slow) * 0.035;
        const t = i / count;
        let sample = (low - slow) * Math.min(1, t * 42) * (1 - t) ** 3;
        for (const crackle of crackles) {
            const age = i - crackle.start;
            if (age >= 0 && age < crackle.length) {
                sample += (white - previous * 0.66) * crackle.level *
                    (1 - age / crackle.length) ** 2 * (1 - t);
            }
        }
        previous = white;
        return sample * Math.min(1, (count - 1 - i) / 42);
    });

    const rms = Math.sqrt(samples.reduce((sum, x) => sum + x * x, 0) / count);
    const gain = 32768 * 10 ** (-34 / 20) / rms;
    return samples.map(x => Math.round(x * gain));
}

// PS1 predictor zero. Each 16-byte block stores 28 signed nibbles.
function encode(samples) {
    const data = [];
    let squareSum = 0;
    let peak = 0;
    for (let offset = 0; offset < samples.length; offset += 28) {
        const block = samples.slice(offset, offset + 28);
        let shift = 12;
        const quantize = x => Math.round(x / (1 << (12 - shift)));
        while (shift > 0 && block.some(x => quantize(x) < -8 || quantize(x) > 7)) shift--;
        const nibbles = block.map(quantize);
        if (nibbles.some(x => x < -8 || x > 7)) throw new Error('ADPCM clipping');
        data.push(shift, offset + 28 === samples.length ? 1 : 0);
        for (let i = 0; i < nibbles.length; i += 2)
            data.push((nibbles[i] & 15) | ((nibbles[i + 1] & 15) << 4));
        for (const nibble of nibbles) {
            const decoded = nibble * (1 << (12 - shift));
            peak = Math.max(peak, Math.abs(decoded));
            squareSum += decoded * decoded;
        }
    }
    const db = x => (20 * Math.log10(x / 32768)).toFixed(2);
    console.log(`${(samples.length / 22.05).toFixed(1)} ms, RMS ${db(Math.sqrt(squareSum / samples.length))} dBFS, peak ${db(peak)} dBFS`);
    return data;
}

const cues = [[0x3bcb184d, 76], [0xb6ad0f27, 84]];
let header = '/* Original procedural leaf-litter cues, 22050 Hz PS1 ADPCM. */\n#include <stdint.h>\n';
for (const [index, [seed, blocks]] of cues.entries()) {
    const data = encode(synthesize(seed, blocks));
    header += `\n#define WOODS_STEP_${index}_ADPCM ${data.length}\n`;
    header += `static const uint8_t woods_step_${index}[] __attribute__((aligned(64))) = {\n`;
    for (let i = 0; i < data.length; i += 16)
        header += ' ' + data.slice(i, i + 16).map(x => '0x' + x.toString(16).padStart(2, '0')).join(',') + ',\n';
    header += '};\n';
}
fs.writeFileSync(output, header);
