// The level editor's grid: a gauntlet's rows, top first (as its file and Gauntlet.h have them), one character a cell.
// Every edit returns new rows (the editor's undo keeps whole grids). No DOM here: tests/grid.test.mjs runs it in Node.
//
// Cells (Gauntlet.h): '#' stone, 'H' cyber cube, 'B' bumper, 'R' / 'L' one-way gate (passed running right / left),
// 'S' a saw in stone, 'C' a saw in a cube, '>' the entry (left edge), 'X' the exit (right edge), '.' air. Row index i
// counts from the top; the game's row is rows.length - 1 - i (0 at the street).

export const CELLS = [
  { ch: '.', name: 'Air', key: '1', tip: 'Carve rooms: the hen runs through air' },
  { ch: '#', name: 'Stone', key: '2', tip: 'Floors, roofs and walls: at her body it crashes her' },
  { ch: 'H', name: 'Cyber cube', key: '3', tip: 'Stone in the cubes’ look' },
  { ch: 'B', name: 'Bumper', key: '4', tip: 'Turns her back; stacked over, she climbs it' },
  { ch: 'R', name: 'Gate →', key: '5', tip: 'One-way gate she passes running right (a bumper running left); never a floor' },
  { ch: 'L', name: 'Gate ←', key: '6', tip: 'One-way gate she passes running left' },
  { ch: 'S', name: 'Saw', key: '7', tip: 'A blade in stone: each crossing takes an egg, or her' },
  { ch: 'C', name: 'Cube saw', key: '8', tip: 'A blade in a cyber cube' },
  { ch: '>', name: 'Entry', key: '9', tip: 'The door in, on the left edge: click a row to move it' },
  { ch: 'X', name: 'Exit', key: '0', tip: 'The door out, on the right edge: click a row to move it' },
];
export const CELL = Object.fromEntries(CELLS.map((c) => [c.ch, c]));
export const DOORS = new Set(['>', 'X']);
export const SOLID = new Set(['#', 'H', 'B', 'S', 'C']); // (gates are no floor, no roof)

const MIRROR = { '>': 'X', X: '>', R: 'L', L: 'R' };

export const cols = (rows) => (rows.length ? rows[0].length : 0);
export const at = (rows, c, i) => (i >= 0 && i < rows.length && c >= 0 && c < cols(rows) ? rows[i][c] : '#');
export const gameRow = (rows, i) => rows.length - 1 - i;
export const index = (rows, r) => rows.length - 1 - r;

function put(row, c, ch) {
  return row.slice(0, c) + ch + row.slice(c + 1);
}

// one cell (doors move with moveDoor; a door cell is never painted over)
export function setCell(rows, c, i, ch) {
  if (i < 0 || i >= rows.length || c < 0 || c >= cols(rows)) return rows;
  if (DOORS.has(ch)) return moveDoor(rows, ch, i);
  if (DOORS.has(rows[i][c]) || rows[i][c] === ch) return rows;
  const out = rows.slice();
  out[i] = put(out[i], c, ch);
  return out;
}

// a rectangle (corners in any order), doors kept
export function fillRect(rows, c0, i0, c1, i1, ch) {
  if (DOORS.has(ch)) return rows;
  const [ca, cb] = [Math.max(0, Math.min(c0, c1)), Math.min(cols(rows) - 1, Math.max(c0, c1))];
  const [ia, ib] = [Math.max(0, Math.min(i0, i1)), Math.min(rows.length - 1, Math.max(i0, i1))];
  const out = rows.slice();
  for (let i = ia; i <= ib; i++) {
    let row = out[i];
    for (let c = ca; c <= cb; c++) if (!DOORS.has(row[c])) row = put(row, c, ch);
    out[i] = row;
  }
  return out;
}

export function findDoor(rows, ch) {
  const c = ch === '>' ? 0 : cols(rows) - 1;
  for (let i = 0; i < rows.length; i++) if (rows[i][c] === ch) return { c, i };
  return null;
}

// The entry ('>', left edge) or the exit ('X', right edge) to row i: its old opening walled up, the new one as tall as
// the passage it opens onto (the air over that row in the next column in)
export function moveDoor(rows, ch, i) {
  const n = cols(rows);
  if (n < 3 || i < 1 || i >= rows.length - 1) return rows;
  const c = ch === '>' ? 0 : n - 1;
  const inner = ch === '>' ? 1 : n - 2;
  const out = rows.map((row) => put(row, c, '#'));
  out[i] = put(out[i], c, ch);
  for (let k = i - 1; k > 0 && out[k][inner] === '.'; k--) out[k] = put(out[k], c, '.');
  return out;
}

// What a level needs of a grid to lay it (Gauntlet::parse): its size, its door's row and its opening's top (the
// first cell up its left edge that isn't air: a lintel before it goes as low), its exit's row; game rows, null
// without both doors
export function doors(rows) {
  const e = findDoor(rows, '>'), x = findDoor(rows, 'X');
  if (!e || !x) return null;
  const entryRow = gameRow(rows, e.i);
  let entryTop = entryRow;
  while (entryTop < rows.length && '.>'.includes(rows[index(rows, entryTop)][0])) entryTop++;
  return { cols: cols(rows), rows: rows.length, entryRow, entryTop, exitRow: gameRow(rows, x.i) };
}

// A cell she can start in (the play-test's start in a maze: Game::debugPlayTest): her feet in it, her body in the
// cell over it too, both air; on a floor (stone, a cube, a bumper: a blade would get her, with no egg under her; a
// gate is no floor); never a door's column. Clicked in the air higher up: where she'd land, down the column.
// { c, i, b } (b: the game's row), or null
export function standable(rows, c, i) {
  const n = cols(rows), air = (ch) => ch === '.';
  if (c < 1 || c > n - 2 || i < 1 || i >= rows.length - 1 || !air(rows[i][c])) return null;
  let k = i;
  while (k + 1 < rows.length && air(rows[k + 1][c])) k++;
  if (k + 1 >= rows.length || !'#HB'.includes(rows[k + 1][c]) || !air(rows[k - 1][c])) return null;
  return { c, i: k, b: gameRow(rows, k) };
}

// A new gauntlet's first grid: a closed room `width` by `height`, its doors a row up off its floor, each 3 high (a
// lintel's height: one egg under it), one way through it (straight along). Design it from there
export function template(width = 40, height = 10) {
  const out = [];
  for (let i = 0; i < height; i++) {
    const r = height - 1 - i;
    if (r === 0 || r === height - 1) { out.push('#'.repeat(width)); continue; }
    const open = r >= 1 && r <= 3;
    out.push((r === 1 ? '>' : open ? '.' : '#') + '.'.repeat(width - 2) + (r === 1 ? 'X' : open ? '.' : '#'));
  }
  return out;
}

// Columns c..c+count-1 copied in right after themselves (a floor, a roof, a room made longer); the doors' columns
// aren't copied: the copy comes from the column next to them
export function duplicateCols(rows, c0, c1 = c0) {
  const n = cols(rows);
  if (n < 3) return rows;
  const inner = (c) => Math.max(1, Math.min(n - 2, c));
  const a = inner(Math.min(c0, c1)), b = inner(Math.max(c0, c1));
  return rows.map((row) => row.slice(0, b + 1) + row.slice(a, b + 1) + row.slice(b + 1));
}

// Columns c0..c1 taken out (never the doors' columns, never down to fewer than 4)
export function deleteCols(rows, c0, c1 = c0) {
  const n = cols(rows);
  const a = Math.max(1, Math.min(c0, c1)), b = Math.min(n - 2, Math.max(c0, c1));
  if (a > b || n - (b - a + 1) < 4) return rows;
  return rows.map((row) => row.slice(0, a) + row.slice(b + 1));
}

// Rows i0..i1 copied in right above themselves (everything above moves up a row; the top row stays the top)
export function duplicateRows(rows, i0, i1 = i0) {
  const a = Math.max(0, Math.min(i0, i1)), b = Math.min(rows.length - 1, Math.max(i0, i1));
  const copy = rows.slice(a, b + 1).map((row) => row.replace(/[>X]/g, '#'));
  return [...rows.slice(0, a), ...copy, ...rows.slice(a)];
}

// Rows i0..i1 taken out (never a door's row, never down to fewer than 4)
export function deleteRows(rows, i0, i1 = i0) {
  const a = Math.max(0, Math.min(i0, i1)), b = Math.min(rows.length - 1, Math.max(i0, i1));
  if (rows.length - (b - a + 1) < 4) return rows;
  for (let i = a; i <= b; i++) if (/[>X]/.test(rows[i])) return rows;
  return [...rows.slice(0, a), ...rows.slice(b + 1)];
}

// ---- selections: a block of cells lifted out, moved, flipped, pasted (doors never move with one: in a block they're
// null, and pasting leaves what's under a null)

export function extract(rows, c0, i0, c1, i1) {
  const block = [];
  for (let i = Math.min(i0, i1); i <= Math.max(i0, i1); i++) {
    const line = [];
    for (let c = Math.min(c0, c1); c <= Math.max(c0, c1); c++) {
      const ch = at(rows, c, i);
      line.push(DOORS.has(ch) ? null : ch);
    }
    block.push(line);
  }
  return block;
}

export function paste(rows, block, c, i) {
  const out = rows.slice();
  for (let y = 0; y < block.length; y++) {
    const ii = i + y;
    if (ii < 0 || ii >= rows.length) continue;
    let row = out[ii];
    for (let x = 0; x < block[y].length; x++) {
      const cc = c + x, ch = block[y][x];
      if (ch === null || cc < 0 || cc >= cols(rows) || DOORS.has(row[cc])) continue;
      row = put(row, cc, ch);
    }
    out[ii] = row;
  }
  return out;
}

// a block mirrored left to right (the gates turned round)
export function flip(block) {
  return block.map((line) => line.slice().reverse().map((ch) => (ch === null ? null : MIRROR[ch] || ch)));
}

// What's left where a block was lifted: each of its rows filled with what that row has just outside the block (left,
// else right): a blade lifted from a floor leaves floor, a pillar lifted from a room leaves air
export function vacate(rows, c0, i0, c1, i1, fallback = '#') {
  const [ca, cb] = [Math.min(c0, c1), Math.max(c0, c1)];
  let out = rows;
  for (let i = Math.min(i0, i1); i <= Math.max(i0, i1); i++) {
    const left = ca > 0 ? at(rows, ca - 1, i) : null, right = cb < cols(rows) - 1 ? at(rows, cb + 1, i) : null;
    const pick = (ch) => (ch && !DOORS.has(ch) && ch !== 'S' && ch !== 'C' ? ch : null);
    out = fillRect(out, ca, i, cb, i, pick(left) || pick(right) || fallback);
  }
  return out;
}

// ---- notes: what reads badly even where the solver's happy (docs/LEVEL_DESIGN.md)
export function lints(rows) {
  const out = [];
  const saw = (ch) => ch === 'S' || ch === 'C';
  for (let i = 0; i < rows.length; i++)
    for (let c = 1; c < cols(rows); c++)
      if (saw(rows[i][c]) && saw(rows[i][c - 1]))
        out.push({ c, i, text: 'Blades side by side: keep them 2 cells apart, so each reads (and cuts) on its own' });
  for (let i = 1; i < rows.length - 1; i++)
    for (let c = 1; c < cols(rows) - 1; c++)
      if (rows[i][c] === '.' && rows[i + 1][c] === '.' && SOLID.has(rows[i + 1][c - 1]) && SOLID.has(rows[i + 1][c + 1]) &&
          rows[i][c - 1] === '.' && rows[i][c + 1] === '.')
        out.push({ c, i: i + 1, text: 'A hole one cell wide: she drops through it, but two wide reads better' });
  return out;
}

// cells that differ from another grid of the same size ("c,i" keys); null if the sizes differ
export function changed(a, b) {
  if (!b || a.length !== b.length || cols(a) !== cols(b)) return null;
  const out = new Set();
  for (let i = 0; i < a.length; i++) {
    if (a[i] === b[i]) continue;
    for (let c = 0; c < a[i].length; c++) if (a[i][c] !== b[i][c]) out.add(c + ',' + i);
  }
  return out;
}

export function same(a, b) {
  return a.length === b.length && a.every((row, i) => row === b[i]);
}
