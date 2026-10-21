// The level editor's course model: a level file (src/core/LevelData.h has the format) read into columns, one a cell
// (0.6 m) along the course, each a slice of a wall (how high in eggs, its kind, a barrier's underside over it) or of a
// maze gauntlet's span; its saws (metres along: each sits in the wall it's on) and disco balls (metres along, metres
// up), its finish line, difficulty band and raids. Every edit returns a new level, or the same one if nothing changed
// (the editor's undo keeps whole levels). No DOM here: tests/course.test.mjs runs it in Node.
//
// A wall is a run of like columns; `cut` marks where the file starts a new one beside a like one (the game treats
// them as two: OVERDRIVE smashes a wall at a time, a barrier's near miss is judged per wall), so a level reads and
// writes back as it was. An edit that makes a column like its neighbour joins them.

export const U = 0.6;
export const KINDS = ['crate', 'hay', 'mix'];
export const MAX_H = 40, MAX_CEIL = 60;
export const SAW_ROW_GAP = 3 * U;      // Level::kSawRowGap: blades in a row, hub to hub
export const SAW_REACH = 0.8;          // Level::kSawReach: the stack meets a blade this far before its hub
export const MIN_LENGTH = 30;

const mm = (v) => Math.round(v * 1000) / 1000;
const street = (cut = false) => ({ g: null, h: 0, kind: 'crate', ceil: 0, cut });
export const alike = (a, b) => !!a && !!b && a.g === b.g && (!!a.g || (a.h === b.h && a.kind === b.kind && a.ceil === b.ceil));

// a number as the game writes it: to the millimetre, no trailing zeros
export function fmt(v) {
  let s = mm(v).toFixed(3);
  s = s.replace(/0+$/, '').replace(/\.$/, '');
  return s === '-0' ? '0' : s;
}

// ---- reading and writing
export function parseLevel(text) {
  const L = { notes: [], length: 0, diffOffset: null, diffScale: 0.35, raids: 'auto', cols: [], saws: [], discos: [] };
  let part = 'head', started = false;
  for (const raw of text.split('\n')) {
    let line = raw.replace(/\r$/, '');
    const semi = line.indexOf(';');
    if (semi >= 0) {
      if (!started && line.slice(0, semi).trim() === '') { let n = line.slice(semi + 1); if (n.startsWith(' ')) n = n.slice(1); L.notes.push(n); continue; }
      line = line.slice(0, semi);
    }
    const w = line.trim().split(/\s+/).filter(Boolean);
    if (!w.length) continue;
    started = true;
    if (w.length === 1 && ['course', 'saws', 'discos'].includes(w[0])) { part = w[0]; continue; }
    if (part === 'head') {
      if (w[0] === 'length') L.length = Number(w[1]);
      else if (w[0] === 'difficulty') { L.diffOffset = Number(w[1]); L.diffScale = Number(w[2]); }
      else if (w[0] === 'raids') L.raids = w[1] === 'auto' || w[1] === 'none' ? w[1] : Number(w[1]);
      continue;
    }
    if (part === 'course') {
      const cells = Number(w[0]);
      if (w[2] === 'gauntlet') { for (let i = 0; i < cells; i++) L.cols.push({ g: w[3], h: 0, kind: 'crate', ceil: 0, cut: i === 0 }); continue; }
      for (let i = 0; i < cells; i++) L.cols.push({ g: null, h: Number(w[1]), kind: w[2], ceil: w[3] ? Number(w[3]) : 0, cut: i === 0 });
    } else if (part === 'saws') for (const t of w) L.saws.push(Number(t));
    else if (part === 'discos') L.discos.push({ x: Number(w[0]), y: Number(w[1]) });
  }
  return L;
}

export function walls(L) { // the course as the file has it: [{ c0, c1, ...a column }], c1 past its last column
  const out = [];
  for (let i = 0; i < L.cols.length;) {
    let j = i + 1;
    while (j < L.cols.length && !L.cols[j].cut && alike(L.cols[i], L.cols[j])) j++;
    out.push({ ...L.cols[i], c0: i, c1: j });
    i = j;
  }
  return out;
}

export const defaultOffset = (number) => 35 * (number - 1);

export function levelText(L, number) {
  const o = [];
  for (const n of L.notes) o.push(n ? '; ' + n : ';');
  o.push('length ' + fmt(L.length));
  o.push('difficulty ' + fmt(L.diffOffset ?? defaultOffset(number)) + ' ' + fmt(L.diffScale));
  o.push('raids ' + (L.raids === 'auto' || L.raids === 'none' ? L.raids : String(L.raids)));
  o.push('course');
  for (const w of walls(L)) o.push(w.g ? `${w.c1 - w.c0} 0 gauntlet ${w.g}` : `${w.c1 - w.c0} ${w.h} ${w.kind}${w.ceil ? ' ' + w.ceil : ''}`);
  o.push('saws');
  const saws = L.saws.map(fmt);
  for (let i = 0; i < saws.length; i += 8) o.push(saws.slice(i, i + 8).join(' '));
  o.push('discos');
  for (const d of L.discos) o.push(fmt(d.x) + ' ' + fmt(d.y));
  return o.join('\n') + '\n';
}

// ---- reading it
export const colAt = (x) => Math.floor(x / U + 1e-9);
export const courseLength = (L) => L.cols.length * U;
export function spans(L) { // the gauntlets in it: { name, c0, c1 }
  return walls(L).filter((w) => w.g).map((w) => ({ name: w.g, c0: w.c0, c1: w.c1 }));
}
export const spanOf = (L, name) => spans(L).find((s) => s.name === name) || null;
export const spanAt = (L, c) => spans(L).find((s) => c >= s.c0 && c < s.c1) || null;
export const heightAt = (L, x) => { const k = L.cols[colAt(x)]; return k && !k.g ? k.h : 0; };
export const inGauntlet = (L, x) => { const k = L.cols[colAt(x)]; return !!(k && k.g); };
export const tallest = (L) => L.cols.reduce((m, k) => Math.max(m, k.g ? 0 : Math.max(k.h, k.ceil ? k.ceil + 1 : 0)), 0);
export function sawNear(L, x, tol = 0.45) {
  let best = -1, bd = tol;
  L.saws.forEach((s, i) => { const d = Math.abs(s - x); if (d <= bd) { bd = d; best = i; } });
  return best;
}
// a saw where the pointer is: on the decimetre, or a row's gap from a saw it's near (blades back to back)
export function snapSaw(L, x, exclude = -1) {
  let best = Math.round(x * 10) / 10, bd = 0.7;
  L.saws.forEach((s, i) => {
    if (i === exclude) return;
    for (const t of [s - SAW_ROW_GAP, s + SAW_ROW_GAP]) if (Math.abs(t - x) < bd) { bd = Math.abs(t - x); best = mm(t); }
  });
  return best;
}

// ---- editing
function withCols(L, cols) { return cols === L.cols ? L : { ...L, cols }; }
function join(cols, c) { // (a column changed: like its neighbours now, one wall with them)
  if (c > 0 && cols[c] && cols[c].cut && alike(cols[c - 1], cols[c])) cols[c] = { ...cols[c], cut: false };
  if (cols[c + 1] && cols[c + 1].cut && alike(cols[c], cols[c + 1])) cols[c + 1] = { ...cols[c + 1], cut: false };
}

// a wall's height (raised from the street: of `kind`; a barrier now on her head goes)
export function setHeight(L, c, h, kind) {
  const k = L.cols[c];
  if (!k || k.g) return L;
  h = Math.max(0, Math.min(MAX_H, Math.round(h)));
  const nk = { ...k, h, kind: kind && (k.h === 0 || h === 0) ? kind : k.kind, ceil: k.ceil && k.ceil >= h + 2 ? k.ceil : 0 };
  if (nk.h === k.h && nk.kind === k.kind && nk.ceil === k.ceil) return L;
  const cols = L.cols.slice();
  cols[c] = nk;
  join(cols, c);
  return withCols(L, cols);
}
export function setKind(L, c, kind) {
  const k = L.cols[c];
  if (!k || k.g || k.kind === kind || !KINDS.includes(kind)) return L;
  const cols = L.cols.slice();
  cols[c] = { ...k, kind };
  join(cols, c);
  return withCols(L, cols);
}
// a barrier's underside over a wall (0: none): 2 eggs over its top at least (her body under it)
export function setCeil(L, c, ceil) {
  const k = L.cols[c];
  if (!k || k.g) return L;
  ceil = Math.max(0, Math.min(MAX_CEIL, Math.round(ceil)));
  if ((ceil && ceil < k.h + 2) || k.ceil === ceil) return L;
  const cols = L.cols.slice();
  cols[c] = { ...k, ceil };
  join(cols, c);
  return withCols(L, cols);
}

// saws: on the open course (a gauntlet's own blades are its grid's), not on top of another
const sawFits = (L, x, exclude = -1) => x > 0 && x < courseLength(L) && !inGauntlet(L, x) && !L.saws.some((s, i) => i !== exclude && Math.abs(s - x) < 0.5);
export function addSaw(L, x) {
  x = mm(x);
  if (!sawFits(L, x)) return L;
  return { ...L, saws: [...L.saws, x].sort((a, b) => a - b) };
}
export function removeSaw(L, i) { return i < 0 || i >= L.saws.length ? L : { ...L, saws: L.saws.filter((_, j) => j !== i) }; }
export function moveSaw(L, i, x) {
  x = mm(x);
  if (i < 0 || i >= L.saws.length || Math.abs(L.saws[i] - x) < 1e-9 || !sawFits(L, x, i)) return L;
  const saws = L.saws.slice();
  saws[i] = x;
  return { ...L, saws: saws.sort((a, b) => a - b) };
}
export function addDisco(L, x, y) {
  if (x < 0 || x > courseLength(L) || y < 0.2) return L;
  return { ...L, discos: [...L.discos, { x: mm(x), y: mm(y) }].sort((p, q) => p.x - q.x) };
}
export function removeDisco(L, i) { return i < 0 || i >= L.discos.length ? L : { ...L, discos: L.discos.filter((_, j) => j !== i) }; }
export function moveDisco(L, i, x, y) {
  if (i < 0 || i >= L.discos.length || x < 0 || x > courseLength(L) || y < 0.2) return L;
  const discos = L.discos.slice();
  discos[i] = { x: mm(x), y: mm(y) };
  return { ...L, discos: discos.sort((p, q) => p.x - q.x) };
}

// ---- columns: copied out, put in (what's after them moves on), taken out (what's after moves back)
const overlapsSpan = (L, a, b) => spans(L).some((s) => a < s.c1 && b >= s.c0);
export function extractCols(L, c0, c1 = c0) {
  const a = Math.max(0, Math.min(c0, c1)), b = Math.min(L.cols.length - 1, Math.max(c0, c1));
  if (b < a || overlapsSpan(L, a, b)) return null;
  const x0 = a * U, x1 = (b + 1) * U;
  return { cols: L.cols.slice(a, b + 1).map((k, i) => ({ ...k, cut: i > 0 && k.cut })), saws: L.saws.filter((x) => x >= x0 && x < x1).map((x) => mm(x - x0)),
    discos: L.discos.filter((d) => d.x >= x0 && d.x < x1).map((d) => ({ x: mm(d.x - x0), y: d.y })) };
}
export function insertCols(L, c, clip) {
  if (!clip || !clip.cols.length || c < 0 || c > L.cols.length) return L;
  const s = spanAt(L, c);
  if (s && c > s.c0) return L; // (never inside a gauntlet)
  const n = clip.cols.length, at = c * U, shift = n * U, on = (x) => (x >= at - 1e-9 ? mm(x + shift) : x);
  return { ...L, cols: [...L.cols.slice(0, c), ...clip.cols.map((k) => ({ ...k })), ...L.cols.slice(c)],
    saws: [...L.saws.map(on), ...clip.saws.map((x) => mm(x + at))].sort((p, q) => p - q),
    discos: [...L.discos.map((d) => ({ ...d, x: on(d.x) })), ...clip.discos.map((d) => ({ x: mm(d.x + at), y: d.y }))].sort((p, q) => p.x - q.x),
    length: L.length >= at - 1e-9 ? mm(L.length + shift) : L.length };
}
// copied in right after themselves (a run, a wall made longer)
export function duplicateCols(L, c0, c1 = c0) {
  const clip = extractCols(L, c0, c1);
  return clip ? insertCols(L, Math.max(c0, c1) + 1, clip) : L;
}
export function deleteCols(L, c0, c1 = c0) {
  const a = Math.max(0, Math.min(c0, c1)), b = Math.min(L.cols.length - 1, Math.max(c0, c1));
  if (b < a || overlapsSpan(L, a, b)) return L;
  const x0 = a * U, x1 = (b + 1) * U, shift = (b - a + 1) * U;
  const length = L.length >= x1 - 1e-9 ? mm(L.length - shift) : L.length > x0 ? x0 : L.length;
  if (length < MIN_LENGTH) return L;
  const back = (x) => (x >= x1 - 1e-9 ? mm(x - shift) : x);
  const cols = [...L.cols.slice(0, a), ...L.cols.slice(b + 1)];
  while (cols.length * U < length - 1e-9) cols.push(street());
  return { ...L, cols, length, saws: L.saws.filter((x) => x < x0 || x >= x1).map(back),
    discos: L.discos.filter((d) => d.x < x0 || d.x >= x1).map((d) => ({ ...d, x: back(d.x) })) };
}

// The finish line (the course runs on to it: street where it's longer)
export function setLength(L, x) {
  const length = Math.max(MIN_LENGTH, mm(x));
  if (Math.abs(length - L.length) < 1e-9) return L;
  const cols = L.cols.slice();
  while (cols.length * U < length - 1e-9) cols.push(street());
  return { ...L, cols, length };
}

// ---- gauntlets: one maze in a level at most (the game's rule: a gauntlet plays in one level only)
// Its door at column c (grid: { cols, entryRow, entryTop, exitRow }): where it was goes back to street; at c, a
// forecourt at its door's height (a reaction's run, and long enough to refill two and a half eggs: Level::placeGauntlet),
// ending under a lintel as low as its door when the door is low; its span; a porch at its exit's height. What was
// there goes, saws and disco balls with it
export const FORECOURT = 19, LINTEL = 3, PORCH = 8;
export function gauntletFootprint(grid, c) { return { a: Math.max(0, c - FORECOURT - LINTEL), c, z: c + grid.cols + PORCH }; }
// (placed where it was: moved, its approach with it)
export function placeGauntlet(L, name, grid, c) {
  c = Math.max(FORECOURT + LINTEL, Math.round(c));
  const { a, z } = gauntletFootprint(grid, c);
  const from = removeGauntlet(L, name);
  const cols = from.cols.slice();
  for (let i = a; i < Math.min(z, cols.length); i++) if (cols[i].g) return L; // (another gauntlet's in the way)
  while (cols.length < z) cols.push(street());
  const lowDoor = grid.entryTop - grid.entryRow - 2 < 8; // (Level::placeGauntlet: no room under its lintel for a full stack)
  for (let i = a; i < c; i++) cols[i] = { g: null, h: grid.entryRow, kind: 'crate', ceil: lowDoor && i >= c - LINTEL ? grid.entryTop : 0, cut: i === a || i === c - LINTEL };
  for (let i = c; i < c + grid.cols; i++) cols[i] = { g: name, h: 0, kind: 'crate', ceil: 0, cut: i === c };
  for (let i = c + grid.cols; i < z; i++) cols[i] = { g: null, h: grid.exitRow, kind: 'crate', ceil: 0, cut: i === c + grid.cols };
  if (cols[z]) cols[z] = { ...cols[z], cut: true };
  const x0 = a * U, x1 = z * U;
  const length = Math.max(from.length, mm(x1 + 6 * U));
  while (cols.length * U < length - 1e-9) cols.push(street());
  return { ...from, cols, length, saws: from.saws.filter((x) => (x < x0 || x >= x1) && !cols[colAt(x)].g), discos: from.discos.filter((d) => d.x < x0 || d.x >= x1) };
}
// taken out, its approach with it (its forecourt, lintel and porch: street again; walls past them stay)
export function removeGauntlet(L, name) {
  const s = spanOf(L, name);
  if (!s) return L;
  const a = Math.max(0, s.c0 - FORECOURT - LINTEL), z = Math.min(L.cols.length, s.c1 + PORCH);
  const cols = L.cols.slice();
  for (let i = a; i < z; i++) if (!cols[i].g || cols[i].g === name) cols[i] = street(cols[i].cut && (i === a || i === z));
  if (cols[z]) cols[z] = { ...cols[z], cut: true };
  for (let i = a; i <= z && i < cols.length; i++) join(cols, i);
  return withCols(L, cols);
}
// its grid grown or shrunk: its span made that wide, what's after it moved on or back
export function fitGauntlet(L, name, width) {
  const s = spanOf(L, name);
  if (!s || s.c1 - s.c0 === width || width < 4) return L;
  const shift = (width - (s.c1 - s.c0)) * U, at = s.c1 * U;
  const span = Array.from({ length: width }, (_, i) => ({ g: name, h: 0, kind: 'crate', ceil: 0, cut: i === 0 }));
  const on = (x) => (x >= at - 1e-9 ? mm(x + shift) : x);
  return { ...L, cols: [...L.cols.slice(0, s.c0), ...span, ...L.cols.slice(s.c1)], saws: L.saws.map(on), discos: L.discos.map((d) => ({ ...d, x: on(d.x) })),
    length: L.length >= at - 1e-9 ? mm(L.length + shift) : L.length };
}

// ---- the play-test's start (Play, Watch the bot: Game::debugPlayTest): on the open course, x metres along on the
// decimetre, 1 m clear of any saw (she starts with no eggs: a blade under her would get her), short of the finish;
// null inside a gauntlet's span (a start there is a cell of its grid)
export function startOnCourse(L, x) {
  if (!(x >= 0) || L.length < 10) return null;
  let s = Math.round(Math.max(1, Math.min(x, L.length - 5)) * 10) / 10;
  for (let pass = 0; pass < 4; pass++) {
    const near = L.saws.find((w) => Math.abs(w - s) < 1);
    if (near === undefined) break;
    s = Math.round((s < near ? near - 1.1 : near + 1.1) * 10) / 10; // (to its nearer side)
  }
  if (s < 0 || s > L.length - 1 || inGauntlet(L, s) || L.saws.some((w) => Math.abs(w - s) < 1)) return null;
  return s;
}

// ---- the campaign's zones (Zone.h)
export const zoneOf = (n) => (n >= 11 && n <= 20 ? 'egypt' : n >= 21 && n <= 30 ? 'japan' : 'city');
export const ZONES = { city: { name: 'The neon city', short: 'City' }, egypt: { name: 'Neon Egypt', short: 'Egypt' }, japan: { name: 'Neon Japan', short: 'Japan' } };
