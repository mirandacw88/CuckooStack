// Dumps the web build's course for a date by evaluating the generator code lifted verbatim from cuckoo-stack.html.
// Usage: node tests/level_parity.mjs 2026-10-06 3000
import { readFileSync } from 'node:fs';
const html = readFileSync(new URL('../cuckoo-stack.html', import.meta.url), 'utf8');
const grab = (start, end) => { const a = html.indexOf(start), b = html.indexOf(end, a); if (a < 0 || b < 0) throw new Error('marker ' + start); return html.slice(a, b); };
const src = [
  'const U = 0.6; const SECTOR = 150; const HEN_H = 1.05; const lerp = (a, b, t) => a + (b - a) * t;',
  grab('function seedFor', '\n') , grab('function mulberry32', '\n'),
  grab('function curve(d)', 'const rnd ='),
  grab('let segs = []', 'const _m = new THREE.Matrix4()'),
  'function spawnCorn(x, y) { corns.push({ x, y }); }',
  'lrand = mulberry32(seedFor("cluckstack:" + DATE)); generateUntil(MAX);',
  'for (const s of segs) out.push(["S", s.x0.toFixed(9), s.x1.toFixed(9), s.h, s.kind, s.ceil || 0].join(" "));',
  'for (const c of corns) out.push(["C", c.x.toFixed(9), c.y.toFixed(9)].join(" "));',
].join('\n');
const out = [];
new Function('DATE', 'MAX', 'out', 'let lrand;\n' + src)(process.argv[2], Number(process.argv[3]), out);
console.log(out.join('\n'));
