// The level editor's course model: a level file (src/core/LevelData.h has the format) read into columns, one a cell
// (0.6 m) along the course, each a slice of a wall (how high in eggs, its kind, a barrier's underside over it, and a
// floating block's top if the barrier is a block's underside) or of a maze gauntlet's span; its saws (metres along: each sits in the wall it's on) and disco balls (metres along, metres
// up), its wall lasers (each set into a wall's or a block's side: the column edge it's on, metres along, its row, eggs
// up from the street, and the way it fires along the course), lava (metres along, cells long) and springs (metres along,
// rows of lift), its finish line, difficulty band and raids. A wall's kind is crates, hay, hay-topped, or a bouncer: a
// bumper or a one-way gate (gate> passed running right, gate< left), at least 1 high, never the course's first wall,
// never under a barrier or a block; each group of them is a bounce zone in the game (bounceZones). Every edit returns a new level, or the same one if nothing changed
// (the editor's undo keeps whole levels). No DOM here: tests/course.test.mjs runs it in Node.
//
// A wall is a run of like columns; `cut` marks where the file starts a new one beside a like one (the game treats
// them as two: OVERDRIVE smashes a wall at a time, a barrier's near miss is judged per wall), so a level reads and
// writes back as it was. An edit that makes a column like its neighbour joins them.

export const U = 0.6;
export const KINDS = ['crate', 'hay', 'mix'];
export const BOUNCERS = ['bumper', 'gate>', 'gate<'];
export const isBouncer = (k) => !!k && !k.g && BOUNCERS.includes(k.kind);
export const ZONE_GAP = 40;                          // (Level::kZoneGap: bouncer walls this many cells apart at the most, one zone)
export const MAX_H = 60, MAX_CEIL = 80;
export const SAW_ROW_GAP = 3 * U;      // Level::kSawRowGap: blades in a row, hub to hub
export const SAW_REACH = 0.8;          // Level::kSawReach: the stack meets a blade this far before its hub
export const MIN_LENGTH = 30;
export const LASER_ROW = { min: 0, max: 14, def: 1 }; // (Hazards: the laser's row, eggs up from the street)
export const LASER_REACH = 26;                       // (Level::kLaserRange: its beam's reach at the most, cells)
export const LASER_SNAP = 2;                         // (a click snaps to a side this many column edges away at most)
export const LAVA_LEN = { min: 1, max: 8, def: 3 };  // (...and lava's length, cells)
export const HAZARD_GAP = 0.5;                       // (their minGap: never closer than this to another of theirs)
export const SPRING_POWER = { min: 2, max: 12, def: 6 }; // (the spring's lift, rows)
export const SPRING_GAP = 1.2;                       // (...its minGap)

const mm = (v) => Math.round(v * 1000) / 1000;
const street = (cut = false) => ({ g: null, h: 0, kind: 'crate', ceil: 0, top: 0, cut });
export const alike = (a, b) => !!a && !!b && a.g === b.g && (!!a.g || (a.h === b.h && a.kind === b.kind && a.ceil === b.ceil && (a.top || 0) === (b.top || 0)));

// a number as the game writes it: to the millimetre, no trailing zeros
export function fmt(v) {
  let s = mm(v).toFixed(3);
  s = s.replace(/0+$/, '').replace(/\.$/, '');
  return s === '-0' ? '0' : s;
}

// ---- reading and writing
// (a section the editor doesn't know, a newer hazard of the game's (Hazards.h): kept line by line in `extra`, written
// back after the others, as the game does)
const sectionWord = (w) => /^[a-z]{3,}$/.test(w) && !['length', 'difficulty', 'raids', 'hard'].includes(w);
export function parseLevel(text) {
  const L = { notes: [], length: 0, diffOffset: null, diffScale: 0.35, raids: 'auto', hard: 0, cols: [], saws: [], discos: [], lasers: [], lava: [], springs: [], extra: [] };
  let part = 'head', started = false;
  for (const raw of text.split('\n')) {
    let line = raw.replace(/\r$/, '');
    const semi = line.indexOf(';');
    if (semi >= 0) {
      if (!started && line.slice(0, semi).trim() === '') { let n = line.slice(semi + 1); if (n.startsWith(' ')) n = n.slice(1); L.notes.push(n); continue; }
      line = line.slice(0, semi);
    }
    const w = line.trim().split(/\s+/).filter(Boolean);
    if (!w.length) { if (part === 'extra' && semi < 0) L.extra.push(line); continue; }
    started = true;
    if (w.length === 1 && ['course', 'saws', 'discos', 'lasers', 'lava', 'springs'].includes(w[0])) { part = w[0]; continue; }
    if (w.length === 1 && part !== 'head' && sectionWord(w[0])) part = 'extra';
    if (part === 'extra') { L.extra.push(line); continue; }
    if (part === 'head') {
      if (w[0] === 'length') L.length = Number(w[1]);
      else if (w[0] === 'difficulty') { L.diffOffset = Number(w[1]); L.diffScale = Number(w[2]); }
      else if (w[0] === 'raids') L.raids = w[1] === 'auto' || w[1] === 'none' ? w[1] : Number(w[1]);
      else if (w[0] === 'hard') L.hard = Number(w[1]); // (super hard: the generator's density 6 to 10, the checker's margins less)
      continue;
    }
    if (part === 'course') {
      const cells = Number(w[0]);
      if (w[2] === 'gauntlet') { for (let i = 0; i < cells; i++) L.cols.push({ g: w[3], h: 0, kind: 'crate', ceil: 0, top: 0, cut: i === 0 }); continue; }
      for (let i = 0; i < cells; i++) L.cols.push({ g: null, h: Number(w[1]), kind: w[2], ceil: w[3] ? Number(w[3]) : 0, top: w[4] ? Number(w[4]) : 0, cut: i === 0 });
    } else if (part === 'saws') for (const t of w) L.saws.push(Number(t));
    else if (part === 'discos') L.discos.push({ x: Number(w[0]), y: Number(w[1]) });
    else if (part === 'lasers') L.lasers.push({ x: Number(w[0]), row: w[1] === undefined ? LASER_ROW.def : Number(w[1]), dir: Number(w[2] || 0) ? 1 : 0 });
    else if (part === 'lava') L.lava.push({ x: Number(w[0]), len: Number(w[1]) });
    else if (part === 'springs') L.springs.push({ x: Number(w[0]), power: w[1] === undefined ? SPRING_POWER.def : Number(w[1]) });
  }
  while (L.extra.length && !L.extra[L.extra.length - 1].trim()) L.extra.pop();
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

// a level file's text laying the gauntlet `from` as `to` (its "N 0 gauntlet NAME" line; the rest as it was: the
// game's renameGauntletIn, LevelData.h)
export function renameIn(text, from, to) {
  return text.split('\n').map((line) => {
    const m = /^(\s*\d+\s+0\s+gauntlet\s+)([a-z][a-z0-9_]*)(\s*(;.*)?)$/.exec(line);
    return m && m[2] === from ? m[1] + to + m[3] : line;
  }).join('\n');
}

export function levelText(L, number) {
  const o = [];
  for (const n of L.notes) o.push(n ? '; ' + n : ';');
  o.push('length ' + fmt(L.length));
  o.push('difficulty ' + fmt(L.diffOffset ?? defaultOffset(number)) + ' ' + fmt(L.diffScale));
  o.push('raids ' + (L.raids === 'auto' || L.raids === 'none' ? L.raids : String(L.raids)));
  if (L.hard) o.push('hard ' + L.hard);
  o.push('course');
  for (const w of walls(L)) o.push(w.g ? `${w.c1 - w.c0} 0 gauntlet ${w.g}` : `${w.c1 - w.c0} ${w.h} ${w.kind}${w.ceil ? ' ' + w.ceil : ''}${w.top ? ' ' + w.top : ''}`);
  o.push('saws');
  const saws = L.saws.map(fmt);
  for (let i = 0; i < saws.length; i += 8) o.push(saws.slice(i, i + 8).join(' '));
  o.push('discos');
  for (const d of L.discos) o.push(fmt(d.x) + ' ' + fmt(d.y));
  if (L.lasers && L.lasers.length) { o.push('lasers'); for (const z of L.lasers) o.push(fmt(z.x) + ' ' + z.row + ' ' + z.dir); } // (written when there are any, as the game does)
  if (L.lava && L.lava.length) { o.push('lava'); for (const f of L.lava) o.push(fmt(f.x) + ' ' + fmt(f.len)); }
  if (L.springs && L.springs.length) { o.push('springs'); for (const p of L.springs) o.push(fmt(p.x) + ' ' + fmt(p.power)); }
  const extra = (L.extra || []).slice();
  while (extra.length && !extra[extra.length - 1].trim()) extra.pop();
  o.push(...extra);
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
// the surface she stands on at x with her feet at `feet` eggs up: a floating block's top once she's up on it (Level::floorAt)
export const floorAt = (L, x, feet) => { const k = L.cols[colAt(x)]; return !k || k.g ? 0 : k.top && feet >= k.top ? k.top : k.h; };
export const inGauntlet = (L, x) => { const k = L.cols[colAt(x)]; return !!(k && k.g); };
export const tallest = (L) => L.cols.reduce((m, k) => Math.max(m, k.g ? 0 : Math.max(k.h, k.top || 0, k.ceil ? k.ceil + 1 : 0)), 0);
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

// a wall laser near x (its index, -1: none; at `row` if given); lava over x (its index, -1: none); a spring near x
export function laserNear(L, x, tol = 0.45, row = null) {
  let best = -1, bd = tol;
  L.lasers.forEach((z, i) => { const d = Math.abs(z.x - x); if (d <= bd && (row === null || z.row === row)) { bd = d; best = i; } });
  return best;
}
export const lavaEnd = (f) => mm(f.x + f.len * U);
export const lavaOver = (L, x) => L.lava.findIndex((f) => x >= f.x - 1e-9 && x < lavaEnd(f));
export function springNear(L, x, tol = 0.45) {
  let best = -1, bd = tol;
  (L.springs || []).forEach((p, i) => { const d = Math.abs(p.x - x); if (d <= bd) { bd = d; best = i; } });
  return best;
}
// the surface under x (eggs): a floating block's top, else the wall's (Level's surfaceAt: what lava sits on)
export const surfaceAt = (L, x) => { const k = L.cols[colAt(x)]; return !k || k.g ? 0 : k.top || k.h; };
// a cell of the course, solid? (a wall under `row`, a floating block over it, a barrier from its underside up, a
// gauntlet's span; none past either end: EditCourse's solidCell, the game's laser.cpp)
export function solidCell(L, col, row) {
  const k = L.cols[col];
  if (!k) return false;
  if (k.g || row < k.h) return true;
  if (k.top) return row >= k.ceil && row < k.top;
  return !!k.ceil && row >= k.ceil;
}
// The wall's or block's side nearest x (metres) at the row y (metres up) is in, within `cols` column edges of it: a
// solid cell on one side, open on the other (never a gauntlet's: its lasers are its grid's): { x, row, dir } (dir 1:
// a right face, firing right), or null (EditCourse's faceNear)
export function faceNear(L, x, y, cols = LASER_SNAP) {
  const row = Math.max(LASER_ROW.min, Math.min(LASER_ROW.max, Math.floor(y / U)));
  const mid = Math.round(x / U);
  for (let k = 0; k <= cols; k++)
    for (const e of [mid - k, mid + k]) {
      if (e < 1 || e > L.cols.length - 1) continue;
      const l = solidCell(L, e - 1, row), r = solidCell(L, e, row);
      if (l === r || L.cols[l ? e - 1 : e].g) continue;
      return { x: mm(e * U), row, dir: l ? 1 : 0 };
    }
  return null;
}
// its beam's reach along the course, [x0, x1] metres: from its face along its row through open cells to the first
// solid one, LASER_REACH cells at the most, or the course's end (EditCourse's faceReach)
export function laserReach(L, z) {
  const d = z.dir ? 1 : -1;
  let e = Math.round(z.x / U);
  for (let k = 0; k < LASER_REACH; k++) {
    const cell = d > 0 ? e : e - 1;
    if (cell < 0 || cell >= L.cols.length || solidCell(L, cell, z.row)) break;
    e += d;
  }
  return [mm(Math.min(z.x, e * U)), mm(Math.max(z.x, e * U))];
}
// the column a laser's set into (it goes with it: copied, moved, taken out)
export const laserCol = (z) => Math.round(z.x / U) - (z.dir ? 1 : 0);
// where the game takes it (laser.cpp's validate): on a column's edge, set into a solid cell (not a gauntlet's), firing
// into an open one; never on top of another
export function laserFits(L, z, exclude = -1) {
  const e = Math.round(z.x / U);
  if (Math.abs(z.x / U - e) > 1e-6 || e < 0 || e > L.cols.length || z.row < LASER_ROW.min || z.row > LASER_ROW.max) return false;
  const inn = z.dir ? e - 1 : e, out = z.dir ? e : e - 1;
  if (!solidCell(L, inn, z.row) || L.cols[inn].g || solidCell(L, out, z.row)) return false;
  return !L.lasers.some((q, i) => i !== exclude && Math.abs(q.x - z.x) < 1e-6 && q.row === z.row && q.dir === z.dir);
}
// (a wall edited from under a laser: it goes with it, as the game wouldn't take it)
const keepLasers = (L) => (L.lasers && L.lasers.some((z, i) => !laserFits(L, z, i)) ? { ...L, lasers: L.lasers.filter((z, i) => laserFits(L, z, i)) } : L);

// ---- editing
function withCols(L, cols) { return cols === L.cols ? L : keepLasers({ ...L, cols }); }
function join(cols, c) { // (a column changed: like its neighbours now, one wall with them)
  if (c > 0 && cols[c] && cols[c].cut && alike(cols[c - 1], cols[c])) cols[c] = { ...cols[c], cut: false };
  if (cols[c + 1] && cols[c + 1].cut && alike(cols[c], cols[c + 1])) cols[c + 1] = { ...cols[c + 1], cut: false };
}

// a wall's height (raised from the street: of `kind`; a barrier now on her head goes; a bouncer down to the street, crates)
export function setHeight(L, c, h, kind) {
  const k = L.cols[c];
  if (!k || k.g) return L;
  h = Math.max(0, Math.min(MAX_H, Math.round(h)));
  const keep = k.ceil && k.ceil >= h + (k.top ? 1 : 2); // (a barrier or block now on her head goes)
  let nkind = kind && KINDS.includes(kind) && (k.h === 0 || h === 0) ? kind : k.kind;
  if (!h && BOUNCERS.includes(nkind)) nkind = 'crate';
  const nk = { ...k, h, kind: nkind, ceil: keep ? k.ceil : 0, top: keep ? k.top || 0 : 0 };
  if (nk.h === k.h && nk.kind === k.kind && nk.ceil === k.ceil && nk.top === (k.top || 0)) return L;
  const cols = L.cols.slice();
  cols[c] = nk;
  join(cols, c);
  return withCols(L, cols);
}
export function setKind(L, c, kind) {
  const k = L.cols[c];
  if (!k || k.g || k.kind === kind || !(KINDS.includes(kind) || (BOUNCERS.includes(kind) && bouncerFits(L, c) && k.h && !k.ceil))) return L;
  const cols = L.cols.slice();
  cols[c] = { ...k, kind };
  join(cols, c);
  return withCols(L, cols);
}
// a bouncer wall (the Bouncers tools): column c a bumper or a gate `h` high, at least 1 (a barrier or block over it
// goes: never one on a bouncer); never the course's first wall (column 0's: she comes to it). 0 high: down to the street
const bouncerFits = (L, c) => c > 0;
export function setBouncer(L, c, h, kind) {
  const k = L.cols[c];
  if (!k || k.g || !BOUNCERS.includes(kind)) return L;
  h = Math.min(MAX_H, Math.round(h));
  if (h < 1) return setHeight(L, c, 0);
  if (!bouncerFits(L, c) || (k.h === h && k.kind === kind && !k.ceil)) return L;
  const cols = L.cols.slice();
  cols[c] = { ...k, h, kind, ceil: 0, top: 0 };
  join(cols, c);
  return withCols(L, cols);
}
// The bounce zones the game makes of them (Level::buildZones): each group of bouncer walls ZONE_GAP cells apart at the
// most, from the column before its first (where she comes to it) to its last: [{ c0, c1 }] (c1 past its last column)
export function bounceZones(L) {
  const out = [];
  L.cols.forEach((k, c) => {
    if (!isBouncer(k)) return;
    const z = out[out.length - 1];
    if (z && c - z.c1 <= ZONE_GAP) z.c1 = c + 1; else out.push({ c0: Math.max(0, c - 1), c1: c + 1 });
  });
  return out;
}
export const zoneAt = (L, c) => bounceZones(L).find((z) => c >= z.c0 && c < z.c1) || null;
// a barrier's underside over a wall (0: none, and no block): 2 eggs over its top at least (her body under it); a
// floating block's underside 1 at least (a block too low to duck under: over it is the only way), under its top
export function setCeil(L, c, ceil) {
  const k = L.cols[c];
  if (!k || k.g || (isBouncer(k) && ceil)) return L; // (never over a bumper or a gate)
  ceil = Math.max(0, Math.min(MAX_CEIL, Math.round(ceil)));
  if ((ceil && ceil < k.h + (k.top ? 1 : 2)) || (ceil && k.top && ceil >= k.top) || k.ceil === ceil) return L;
  const cols = L.cols.slice();
  cols[c] = { ...k, ceil, top: ceil ? k.top || 0 : 0 };
  join(cols, c);
  return withCols(L, cols);
}

// a floating block over a wall: its underside (1 egg over the wall at least) and its top (over its underside, up to
// MAX_H); (0, 0): none (a barrier under it goes too)
// (of `kind` over the street: a block's kind is its wall's)
export function setBlock(L, c, bottom, top, kind) {
  const k = L.cols[c];
  if (!k || k.g) return L;
  bottom = Math.round(bottom); top = Math.round(top);
  if (!top) { if (!k.top) return L; const cols = L.cols.slice(); cols[c] = { ...k, ceil: 0, top: 0 }; join(cols, c); return withCols(L, cols); }
  if (isBouncer(k)) return L; // (never over a bumper or a gate)
  const nkind = kind && !k.h && KINDS.includes(kind) ? kind : k.kind;
  if (bottom < k.h + 1 || top <= bottom || top > MAX_H || (k.ceil === bottom && k.top === top && k.kind === nkind)) return L;
  const cols = L.cols.slice();
  cols[c] = { ...k, kind: nkind, ceil: bottom, top };
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

// wall lasers: a click (x metres along, y metres up) snaps to the nearest wall's or block's side at its row (faceNear),
// firing away from it; no side near: none. Dragged, to the side nearest the finger.
const byX = (p, q) => p.x - q.x || p.row - q.row;
export function addLaser(L, x, y) {
  const z = faceNear(L, x, y);
  if (!z || !laserFits(L, z)) return L;
  return { ...L, lasers: [...L.lasers, z].sort(byX) };
}
export function removeLaser(L, i) { return i < 0 || i >= L.lasers.length ? L : { ...L, lasers: L.lasers.filter((_, j) => j !== i) }; }
export function moveLaser(L, i, x, y) {
  if (i < 0 || i >= L.lasers.length) return L;
  const z = faceNear(L, x, y), was = L.lasers[i];
  if (!z || (Math.abs(was.x - z.x) < 1e-9 && was.row === z.row && was.dir === z.dir) || !laserFits(L, z, i)) return L;
  const lasers = L.lasers.slice();
  lasers[i] = z;
  return { ...L, lasers: lasers.sort(byX) };
}
// the index of a laser like z (after an edit sorted them)
export const laserIndex = (L, z) => L.lasers.findIndex((q) => Math.abs(q.x - z.x) < 1e-9 && q.row === z.row && q.dir === z.dir);
// lava: 1 to 8 cells along the open course, short of its end, clear of a gauntlet's span and of other lava
const runs = (n) => Math.max(LAVA_LEN.min, Math.min(LAVA_LEN.max, Math.round(n)));
function lavaFits(L, x, len, exclude = -1) {
  const x1 = x + len * U;
  if (x < 0 || x1 > courseLength(L) + 1e-9) return false;
  for (let c = colAt(x); c * U < x1 - 1e-9; c++) if (L.cols[c] && L.cols[c].g) return false;
  return !L.lava.some((f, i) => i !== exclude && (Math.abs(f.x - x) < HAZARD_GAP || (x < lavaEnd(f) - 1e-9 && f.x < x1 - 1e-9)));
}
export function addLava(L, x, len = LAVA_LEN.def) {
  x = mm(x); len = runs(len);
  if (!lavaFits(L, x, len)) return L;
  return { ...L, lava: [...L.lava, { x, len }].sort(byX) };
}
export function removeLava(L, i) { return i < 0 || i >= L.lava.length ? L : { ...L, lava: L.lava.filter((_, j) => j !== i) }; }
export function moveLava(L, i, x, len) {
  x = mm(x);
  if (i < 0 || i >= L.lava.length) return L;
  len = runs(len ?? L.lava[i].len);
  if ((Math.abs(L.lava[i].x - x) < 1e-9 && L.lava[i].len === len) || !lavaFits(L, x, len, i)) return L;
  const lava = L.lava.slice();
  lava[i] = { x, len };
  return { ...L, lava: lava.sort(byX) };
}
// springs: on the open course on the decimetre (its middle), short of its end, clear of a gauntlet's span (0.3 m: its
// pad's half; Spring.cpp's validate) and 1.2 m from another; 2 to 12 rows of lift
const lift = (n) => Math.max(SPRING_POWER.min, Math.min(SPRING_POWER.max, Math.round(n)));
const deci = (x) => mm(Math.round(x * 10) / 10);
function springFits(L, x, exclude = -1) {
  if (x < 0.3 || x > courseLength(L) - 0.3) return false;
  if (spans(L).some((s) => x > s.c0 * U - 0.3 && x < s.c1 * U + 0.3)) return false;
  return !L.springs.some((p, i) => i !== exclude && Math.abs(p.x - x) < SPRING_GAP - 1e-9);
}
export function addSpring(L, x, power = SPRING_POWER.def) {
  x = deci(x); power = lift(power);
  if (!springFits(L, x)) return L;
  return { ...L, springs: [...L.springs, { x, power }].sort(byX) };
}
export function removeSpring(L, i) { return i < 0 || i >= L.springs.length ? L : { ...L, springs: L.springs.filter((_, j) => j !== i) }; }
export function moveSpring(L, i, x, power) {
  if (i < 0 || i >= L.springs.length) return L;
  x = deci(x); power = lift(power ?? L.springs[i].power);
  if ((Math.abs(L.springs[i].x - x) < 1e-9 && L.springs[i].power === power) || !springFits(L, x, i)) return L;
  const springs = L.springs.slice();
  springs[i] = { x, power };
  return { ...L, springs: springs.sort(byX) };
}
export const setSpringPower = (L, i, power) => (i < 0 || i >= L.springs.length ? L : moveSpring(L, i, L.springs[i].x, power));

// ---- columns: copied out, put in (what's after them moves on), taken out (what's after moves back)
const overlapsSpan = (L, a, b) => spans(L).some((s) => a < s.c1 && b >= s.c0);
export function extractCols(L, c0, c1 = c0) {
  const a = Math.max(0, Math.min(c0, c1)), b = Math.min(L.cols.length - 1, Math.max(c0, c1));
  if (b < a || overlapsSpan(L, a, b)) return null;
  const x0 = a * U, x1 = (b + 1) * U;
  return { cols: L.cols.slice(a, b + 1).map((k, i) => ({ ...k, cut: i > 0 && k.cut })), saws: L.saws.filter((x) => x >= x0 && x < x1).map((x) => mm(x - x0)),
    discos: L.discos.filter((d) => d.x >= x0 && d.x < x1).map((d) => ({ x: mm(d.x - x0), y: d.y })),
    lasers: L.lasers.filter((z) => laserCol(z) >= a && laserCol(z) <= b).map((z) => ({ ...z, x: mm(z.x - x0) })),
    lava: L.lava.filter((f) => f.x >= x0 && lavaEnd(f) <= x1 + 1e-9).map((f) => ({ x: mm(f.x - x0), len: f.len })),
    springs: L.springs.filter((p) => p.x >= x0 && p.x < x1).map((p) => ({ x: mm(p.x - x0), power: p.power })) };
}
export function insertCols(L, c, clip) {
  if (!clip || !clip.cols.length || c < 0 || c > L.cols.length) return L;
  const s = spanAt(L, c);
  if (s && c > s.c0) return L; // (never inside a gauntlet)
  if (c === 0 && isBouncer(clip.cols[0])) return L; // (never the course's first wall)
  const n = clip.cols.length, at = c * U, shift = n * U, on = (x) => (x >= at - 1e-9 ? mm(x + shift) : x);
  return keepLasers({ ...L, cols: [...L.cols.slice(0, c), ...clip.cols.map((k) => ({ ...k })), ...L.cols.slice(c)],
    saws: [...L.saws.map(on), ...clip.saws.map((x) => mm(x + at))].sort((p, q) => p - q),
    discos: [...L.discos.map((d) => ({ ...d, x: on(d.x) })), ...clip.discos.map((d) => ({ x: mm(d.x + at), y: d.y }))].sort((p, q) => p.x - q.x),
    lasers: [...L.lasers.map((z) => (laserCol(z) >= c ? { ...z, x: mm(z.x + shift) } : z)), ...(clip.lasers || []).map((z) => ({ ...z, x: mm(z.x + at) }))].sort(byX),
    lava: [...L.lava.map((f) => ({ ...f, x: on(f.x) })), ...(clip.lava || []).map((f) => ({ x: mm(f.x + at), len: f.len }))].sort(byX),
    springs: [...L.springs.map((p) => ({ ...p, x: on(p.x) })), ...(clip.springs || []).map((p) => ({ x: mm(p.x + at), power: p.power }))].sort(byX),
    length: L.length >= at - 1e-9 ? mm(L.length + shift) : L.length });
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
  if (isBouncer(cols[0])) return L; // (a bouncer would be the course's first wall)
  while (cols.length * U < length - 1e-9) cols.push(street());
  return keepLasers({ ...L, cols, length, saws: L.saws.filter((x) => x < x0 || x >= x1).map(back),
    discos: L.discos.filter((d) => d.x < x0 || d.x >= x1).map((d) => ({ ...d, x: back(d.x) })),
    lasers: L.lasers.filter((z) => laserCol(z) < a || laserCol(z) > b).map((z) => (laserCol(z) > b ? { ...z, x: mm(z.x - shift) } : z)),
    lava: L.lava.filter((f) => lavaEnd(f) <= x0 + 1e-9 || f.x >= x1 - 1e-9).map((f) => ({ ...f, x: back(f.x) })), // (lava the cut runs into goes; lasers set in it too)
    springs: L.springs.filter((p) => p.x < x0 || p.x >= x1).map((p) => ({ ...p, x: back(p.x) })) });
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
// there goes, saws, disco balls, lasers, lava and springs with it. Their sizes are the game's (useRules: level_check --registry-json, the
// longest forecourt of any level; these till then): FORECOURT is the run before the lintel
export let FORECOURT = 20, LINTEL = 3, PORCH = 8, BODY_ROWS = 2, BUDGET_CAP = 8;
export function useRules(r) {
  if (!r || !r.ok) return;
  LINTEL = r.lintel; PORCH = r.porch; BODY_ROWS = r.bodyRows; BUDGET_CAP = r.budgetCap;
  FORECOURT = r.forecourt - r.lintel; // (the game's forecourt counts its lintel)
}
export function gauntletFootprint(grid, c) { return { a: Math.max(0, c - FORECOURT - LINTEL), c, z: c + grid.cols + PORCH }; }
// Is a saw, lava or spring the stair's walls were raised under still fair where it stands? As the checker finds it
// (EditCourse.cpp fairOnStair, at the strictest margins): on one flat, a landing's run after a drop, a reaction's run
// before the next wall. kind: 'saw', 'lava' or 'spring'; x and its end xe (lava: its length)
export function fairOnStair(cols, kind, x, xe = x, speed = 12.4) {
  const n = cols.length, i = colAt(x);
  if (i < 0 || i >= n || cols[i].g) return true;
  let s0 = i, s1 = i + 1;
  while (s0 > 0 && !cols[s0].cut && alike(cols[s0 - 1], cols[s0])) s0--;
  while (s1 < n && !cols[s1].cut && alike(cols[s1 - 1], cols[s1])) s1++;
  const reach = kind === 'saw' ? SAW_REACH : kind === 'lava' ? 0.05 : 0.3;
  if (x - reach < s0 * U - 1e-6 || xe + reach > s1 * U + 1e-6) return false;
  const floor = cols[i].h;
  if (s0 > 0 && !cols[s0 - 1].g && cols[s0 - 1].h > floor) {
    const drop = cols[s0 - 1].h - floor, need = Math.max(speed * 0.6, speed * Math.sqrt(2 * drop * U / 36) + 0.3);
    if (x - (kind === 'saw' ? SAW_REACH : 0) - s0 * U < need - 1e-6) return false;
  }
  if (s1 < n && kind !== 'spring' && s1 * U - xe < Math.max(2.5 * U, speed * 0.45 + (kind === 'saw' ? 0.5 : 0)) - 1e-6) return false;
  return true;
}
// (placed where it was: moved, its approach with it)
export function placeGauntlet(L, name, grid, c) {
  c = Math.max(FORECOURT + LINTEL, Math.round(c));
  const { a, z } = gauntletFootprint(grid, c);
  const from = removeGauntlet(L, name);
  const cols = from.cols.slice();
  for (let i = a; i < Math.min(z, cols.length); i++) if (cols[i].g) return L; // (another gauntlet's in the way)
  while (cols.length < z) cols.push(street());
  const lowDoor = grid.entryTop - grid.entryRow - BODY_ROWS < BUDGET_CAP; // (rules::doorNeedsLintel: no room under it for a full stack)
  // (at the height of the wall before it: one with it, its kind, so a landing onto it is whole)
  const before = a > 0 ? cols[a - 1] : null, onFrom = !!before && !before.g && !isBouncer(before) && before.h === grid.entryRow && !before.ceil && !before.top;
  const fore = (i) => ({ g: null, h: grid.entryRow, kind: onFrom ? before.kind : 'crate', ceil: lowDoor && i >= c - LINTEL ? grid.entryTop : 0, top: 0 });
  // (one wall with a like one before it: its landing whole)
  for (let i = a; i < c; i++) cols[i] = { ...fore(i), cut: (i === a && !(a > 0 && alike(cols[a - 1], fore(a)))) || i === c - LINTEL };
  for (let i = c; i < c + grid.cols; i++) cols[i] = { g: name, h: 0, kind: 'crate', ceil: 0, top: 0, cut: i === c };
  for (let i = c + grid.cols; i < z; i++) cols[i] = { g: null, h: grid.exitRow, kind: 'crate', ceil: 0, top: 0, cut: i === c + grid.cols };
  if (cols[z]) cols[z] = { ...cols[z], cut: true };
  const riders = [...from.saws.map((x) => ({ x, xe: x, reach: SAW_REACH, ahead: Math.max(2.5 * U, STAIR_SPEED * 0.45 + 0.5), dropReach: SAW_REACH })),
    ...from.lava.map((f) => ({ x: f.x, xe: lavaEnd(f), reach: 0.05, ahead: Math.max(2.5 * U, STAIR_SPEED * 0.45), dropReach: 0 })),
    ...from.springs.map((q) => ({ x: q.x, xe: q.x, reach: 0.3, ahead: 0.3, dropReach: 0 }))].filter((r) => r.x >= z * U - 1e-9);
  const end = stairDown(cols, z, grid.exitRow, riders);
  const stair = stairUp(cols, z, grid.exitRow);
  const x0 = a * U, x1 = z * U, x2 = end * U;
  const length = Math.max(from.length, mm(x1 + 6 * U));
  while (cols.length * U < length - 1e-9) cols.push(street());
  const buried = (d) => d.x >= x1 && d.x <= x2 && d.y < surfaceAt({ cols }, d.x) * U + 0.3; // (a disco ball the stair covered)
  const left = (x, kind, xe) => x < x1 || x > x2 || fairOnStair(cols, kind, x, xe); // (past the porch, on the stair: unless the stair left it unfair)
  const placed = keepLasers({ ...from, cols, length, saws: from.saws.filter((x) => (x < x0 || x >= x1) && !cols[colAt(x)].g && left(x, 'saw', x)), discos: from.discos.filter((d) => (d.x < x0 || d.x >= x1) && !buried(d)),
    lasers: from.lasers.filter((q) => (laserCol(q) < a || laserCol(q) >= z) && !(q.x >= x1 && q.x <= x2)), lava: from.lava.filter((f) => (lavaEnd(f) <= x0 + 1e-9 || f.x >= x1 - 1e-9) && left(f.x, 'lava', lavaEnd(f))),
    springs: from.springs.filter((p) => (p.x < x0 || p.x >= x1) && left(p.x, 'spring', p.x)) });
  if (!stair.length) return placed;
  const up = insertCols(placed, z, { cols: stair, saws: [], discos: [] }); // (the course after it moves on)
  up.cols[z + stair.length] = { ...up.cols[z + stair.length], cut: true }; // (the wall: whole)
  return up;
}
// ...and, off a porch onto a wall too tall to climb (more than the curve's tallest climb above it, or than a careful
// player's meter holds: 1 egg and 8 cells' refill after the maze, then the porch's cells), a stair up laid in front of the
// wall (EditCourse.cpp stairUp; Level::affordClimb's rules at the run's top speed): each climb 4 at the most, the meter's
// margin to spare, on a run-up that refills it. Its columns (none: the wall is climbable as it is)
const STAIR_EPC = U / (12.4 * 0.4 * 1.1), STAIR_MAX_UP = 4, STAIR_MIN_LEN = 4, BUDGET_MARGIN = 0.4, MAX_RUNUP = 40; // (eggs a cell; Difficulty.cpp, Level.h)
export function stairUp(cols, z, h) {
  if (z >= cols.length) return [];
  const wall = cols[z];
  if (wall.g || wall.top || wall.h <= h) return [];
  const cap = BUDGET_CAP, H = wall.h;
  let b = Math.min(cap, Math.min(cap, 1 + 8 * STAIR_EPC) + PORCH * STAIR_EPC);
  if (H - h <= STAIR_MAX_UP && b + 1e-6 >= H - h + BUDGET_MARGIN) return [];
  const kind = z > 0 && !isBouncer(cols[z - 1]) ? cols[z - 1].kind : 'crate', out = [];
  let cur = h;
  while (cur < H) {
    const k = Math.max(1, Math.min(H - cur, STAIR_MAX_UP, Math.floor(cap - BUDGET_MARGIN)));
    const len = Math.min(Math.max(STAIR_MIN_LEN, Math.ceil(Math.max(0, k + BUDGET_MARGIN - b) / STAIR_EPC - 1e-9)), MAX_RUNUP + STAIR_MIN_LEN);
    b = Math.max(0, Math.min(cap, b + len * STAIR_EPC) - k);
    for (let i = 0; i < len; i++) out.push({ g: null, h: cur, kind, ceil: 0, top: 0, cut: cur > h && i === 0 }); // (the first plateau is the porch's own)
    cur += k;
  }
  return out;
}
// Off a gauntlet's porch (column z on, its exit's height h): down a stair to the course, as the iPhone and iPad
// editor's (EditCourse.cpp stairDown): a drop of 2 at the most a step (or less, onto the highest wall its landing would
// cover), each onto a landing long enough to land and react on at the run's top speed (rules::landingCells), never onto
// a barrier or a block, the walls under it raised to it (a barrier or a block over one goes), one wall a step; it ends
// where the course is as high as it
const STAIR_SPEED = 12.4; // (Difficulty.cpp: the run's top speed, so a landing's never short)
export function landingCells(drop, speed = STAIR_SPEED) { return Math.ceil(speed * (Math.sqrt(2 * drop * U / 36) + 0.45) / U - 1e-9); }
const E_before = (e, r) => e * U - r.xe >= Math.max(r.ahead, r.reach) - 1e-6;
const E_after = (e, r, react) => r.x - r.dropReach - e * U >= react - 1e-6 && r.x - r.reach >= e * U - 1e-6;
export function stairDown(cols, z, h, riders = []) {
  const n = cols.length, kind = z > 0 && !isBouncer(cols[z - 1]) ? cols[z - 1].kind : 'crate';
  const raise = (j, to) => {
    const k = cols[j];
    if (k.h > to) return;
    cols[j] = k.h < to ? { ...k, h: to, ceil: 0, top: 0, kind, cut: false } : { ...k, kind, cut: false };
  };
  // (a saw, lava or spring the course has: a drop's edge goes only where each is clear of it as the checker wants, on the
  // flat before it (a reaction's run to the wall) or well after it (landed, and a reaction): else the flat runs on a cell,
  // up to 40; EditCourse.cpp stairDown)
  const react = STAIR_SPEED * 0.6;
  const clearEdge = (e) => riders.every((r) => E_before(e, r) || E_after(e, r, react));
  let i = z, defer = 0;
  while (h > 0 && i < n && !cols[i].g && cols[i].h < h) {
    let high = 0, blocked = false; // (a maze, a barrier or a block where it would land: no room to land and react under it)
    for (let j = i; j < Math.min(n, i + landingCells(2)); j++) { high = Math.max(high, cols[j].h); blocked = blocked || !!cols[j].g || !!cols[j].ceil || !!cols[j].top; }
    if (blocked || high >= h) { raise(i++, h); continue; } // (not yet: on at its height to the wall ahead, or over what's in the way)
    if (defer < 40 && !clearEdge(i)) { raise(i++, h); defer++; continue; } // (...or past a hazard the edge would crowd)
    defer = 0;
    const next = Math.max(h - 2, high), land = landingCells(h - next);
    for (let j = i; j < Math.min(n, i + land); j++) raise(j, next);
    cols[i] = { ...cols[i], cut: true };
    h = next;
    i += land;
  }
  if (i < n) cols[i] = { ...cols[i], cut: true };
  return i;
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
  const span = Array.from({ length: width }, (_, i) => ({ g: name, h: 0, kind: 'crate', ceil: 0, top: 0, cut: i === 0 }));
  const on = (x) => (x >= at - 1e-9 ? mm(x + shift) : x);
  return keepLasers({ ...L, cols: [...L.cols.slice(0, s.c0), ...span, ...L.cols.slice(s.c1)], saws: L.saws.map(on), discos: L.discos.map((d) => ({ ...d, x: on(d.x) })),
    lasers: L.lasers.map((z) => (laserCol(z) >= s.c1 ? { ...z, x: mm(z.x + shift) } : z)), lava: L.lava.map((f) => ({ ...f, x: on(f.x) })),
    springs: L.springs.map((p) => ({ ...p, x: on(p.x) })), length: L.length >= at - 1e-9 ? mm(L.length + shift) : L.length });
}

// ---- the play-test's start (Play, Watch the bot: Game::debugPlayTest): on the open course, x metres along on the
// decimetre, 1 m clear of any saw (she starts with no eggs: a blade under her would get her) and of any spring (she'd
// start in the air), off lava (it would burn her), short of the finish;
// null inside a gauntlet's span (a start there is a cell of its grid)
export function startOnCourse(L, x) {
  if (!(x >= 0) || L.length < 10) return null;
  let s = Math.round(Math.max(1, Math.min(x, L.length - 5)) * 10) / 10;
  const pads = [...L.saws, ...(L.springs || []).map((p) => p.x)];
  for (let pass = 0; pass < 4; pass++) {
    const near = pads.find((w) => Math.abs(w - s) < 1);
    if (near === undefined) break;
    s = Math.round((s < near ? near - 1.1 : near + 1.1) * 10) / 10; // (to its nearer side)
  }
  if (s < 0 || s > L.length - 1 || inGauntlet(L, s) || pads.some((w) => Math.abs(w - s) < 1) || L.lava.some((f) => s > f.x - 1 && s < lavaEnd(f) + 0.5)) return null;
  return s;
}

// ---- the campaign's zones (Zone.h)
export const zoneOf = (n) => (n >= 11 && n <= 20 ? 'egypt' : n >= 21 && n <= 30 ? 'japan' : 'city');
export const ZONES = { city: { name: 'The neon city', short: 'City' }, egypt: { name: 'Neon Egypt', short: 'Egypt' }, japan: { name: 'Neon Japan', short: 'Japan' } };
