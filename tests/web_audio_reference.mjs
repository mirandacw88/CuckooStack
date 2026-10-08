// Renders the web build's own audio code (tone/noise/sfx/music lifted verbatim from cuckoo-stack.html) through a
// real Web Audio implementation (npm: node-web-audio-api) in an OfflineAudioContext, using the same script as
// tests/render_audio.cpp: 60 s of music with the tempo rising, power-down, then every sound effect.
//   NODE_PATH=<dir with node_modules> node tests/web_audio_reference.mjs out.wav [seconds]
import { readFileSync, writeFileSync } from 'node:fs';
import { createRequire } from 'node:module';
const require = createRequire(process.env.NODE_PATH ? process.env.NODE_PATH + '/' : import.meta.url);
const { OfflineAudioContext } = require('node-web-audio-api');

const out = process.argv[2] || 'web.wav';
const seconds = Number(process.argv[3] || 66);
const sr = 48000;
const html = readFileSync(new URL('../cuckoo-stack.html', import.meta.url), 'utf8');
const grab = (start, end) => { const a = html.indexOf(start), b = html.indexOf(end, a); if (a < 0 || b < 0) throw new Error('marker ' + start); return html.slice(a, b); };
const src = grab('function tone(', '// ---------- music') + grab('const music = (() => {', 'const buzz');

const ctx = new OfflineAudioContext({ numberOfChannels: 1, length: Math.ceil(seconds * sr), sampleRate: sr });
let intervalCb = null;
const env = {
  localStorage: { getItem: () => null, setItem() {} },
  setInterval: (cb) => { intervalCb = cb; return 1; }, clearInterval: () => { intervalCb = null; }, setTimeout: () => 0,
};
const api = new Function('ac', 'localStorage', 'setInterval', 'clearInterval', 'setTimeout', src + '\nreturn { sfx, music };')(
  ctx, env.localStorage, env.setInterval, env.clearInterval, env.setTimeout);

// drive the 25 ms scheduler interval and the script by suspending the offline context at each tick
const musicSecs = seconds - 6;
const actions = new Map();
const at = (t, fn) => { const k = Math.round(t * 1000) / 1000; (actions.get(k) || actions.set(k, []).get(k)).push(fn); };
at(0, () => api.music.start());
for (let t = 0; t < musicSecs; t += 0.5) at(t, () => api.music.tempo(8 + Math.min(4.4, t * 0.05)));
at(musicSecs, () => api.music.stop(true));
const sfxNames = ['lay', 'crack', 'perfect', 'corn', 'land', 'squawk', 'empty'];
sfxNames.forEach((n, i) => at(musicSecs + 1.5 + i * 0.6, () => api.sfx[n](3)));
for (let t = 0; t < seconds - 0.05; t += 0.025) at(t, () => intervalCb && intervalCb());
const times = [...actions.keys()].sort((a, b) => a - b);
for (const t of times) ctx.suspend(t).then(() => { for (const fn of actions.get(t)) fn(); ctx.resume(); });
const buf = await ctx.startRendering();
const mix = buf.getChannelData(0);

let peak = 0, clipped = 0, nans = 0;
for (const v of mix) { if (!Number.isFinite(v)) nans++; peak = Math.max(peak, Math.abs(v)); if (Math.abs(v) >= 0.999) clipped++; }
console.log(`samples ${mix.length}, peak ${peak.toFixed(3)}, clipped ${clipped}, NaN ${nans}`);
const bar = 60 / 126 * 4;
['A intro (bars 0-7)', 'B stabs (8-15)', 'C build (16-23)', 'D drop (24-31)'].forEach((name, s) => {
  const a = Math.floor(s * 8 * bar * sr), b = Math.min(mix.length, Math.floor((s + 1) * 8 * bar * sr));
  let sum = 0; for (let i = a; i < b; i++) sum += mix[i] * mix[i];
  console.log(`  ${name.padEnd(20)} RMS ${(10 * Math.log10(sum / (b - a) + 1e-12)).toFixed(1).padStart(6)} dBFS`);
});
const pcm = Buffer.alloc(44 + mix.length * 2);
pcm.write('RIFF', 0); pcm.writeUInt32LE(36 + mix.length * 2, 4); pcm.write('WAVEfmt ', 8); pcm.writeUInt32LE(16, 16);
pcm.writeUInt16LE(1, 20); pcm.writeUInt16LE(1, 22); pcm.writeUInt32LE(sr, 24); pcm.writeUInt32LE(sr * 2, 28); pcm.writeUInt16LE(2, 32); pcm.writeUInt16LE(16, 34);
pcm.write('data', 36); pcm.writeUInt32LE(mix.length * 2, 40);
for (let i = 0; i < mix.length; i++) pcm.writeInt16LE(Math.round(Math.max(-1, Math.min(1, mix[i])) * 32767), 44 + i * 2);
writeFileSync(out, pcm);
