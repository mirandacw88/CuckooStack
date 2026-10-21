// The level editor's course model (static/js/course.js), in Node (ctest "level_editor_js"): every level the game
// ships reads and writes back as it was (the game's own writer's text, two like walls side by side kept two); walls
// raised and lowered, their kinds, barriers kept off her head, a changed column joining its like neighbours; saws (in a
// row, a blade's gap apart) and disco balls; columns copied in, pasted and taken out, what's after them moved along;
// the finish line; a gauntlet placed with its door's approach, moved, taken out, refitted to its grid.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, readdirSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import path from 'node:path';
import * as K from '../static/js/course.js';

const levels = path.join(path.dirname(fileURLToPath(import.meta.url)), '..', '..', '..', 'assets', 'levels');
const small = `; A test course.
length 30
difficulty 70 0.35
raids auto
course
16 0 crate
5 2 hay 6
4 3 mix
30 0 crate
saws
14.1 22.5
discos
10.5 1.45
`;
const wall = (k) => ({ h: k.h, kind: k.kind, ceil: k.ceil });
const near = (a, b) => Math.abs(a - b) < 1e-9;

test('every shipped level reads and writes back as it was', () => {
  const files = readdirSync(levels).filter((f) => /^level_\d+\.txt$/.test(f));
  assert.ok(files.length >= 30);
  for (const f of files) {
    const text = readFileSync(path.join(levels, f), 'utf8');
    assert.equal(K.levelText(K.parseLevel(text), Number(f.match(/\d+/)[0])), text, f);
  }
});

test('a level reads into columns, a cell each', () => {
  const L = K.parseLevel(small);
  assert.equal(L.cols.length, 55);
  assert.deepEqual(wall(L.cols[16]), { h: 2, kind: 'hay', ceil: 6 });
  assert.deepEqual([L.length, L.diffOffset, L.raids, L.saws.length, L.discos[0].y], [30, 70, 'auto', 2, 1.45]);
  assert.equal(K.levelText(L, 4), small);
  const two = K.parseLevel(small.replace('16 0 crate', '6 0 crate\n10 0 crate'));
  assert.equal(K.walls(two).length, 5, 'two like walls side by side stay two');
  assert.ok(K.levelText(two, 4).includes('\n6 0 crate\n10 0 crate\n'));
  assert.equal(K.parseLevel('length 9\nraids 3\ncourse\n20 0 crate\n').raids, 3);
});

test('walls: raised, lowered, their kinds; a barrier never on her head', () => {
  const L = K.parseLevel(small);
  const up = K.setHeight(L, 3, 2, 'mix');
  assert.deepEqual(wall(up.cols[3]), { h: 2, kind: 'mix', ceil: 0 }, 'street raised: the brush’s kind');
  assert.equal(K.setHeight(up, 3, 4, 'crate').cols[3].kind, 'mix', 'a wall raised more keeps its kind');
  assert.equal(K.setHeight(L, 16, 5).cols[16].ceil, 0, 'a barrier now on her head goes');
  assert.equal(K.setHeight(L, 16, 2), L, 'nothing changed: the same level');
  assert.equal(K.setKind(L, 16, 'crate').cols[16].kind, 'crate');
  assert.equal(K.setKind(L, 16, 'brick'), L);
  assert.equal(K.setCeil(L, 0, 1), L, 'no barrier on her head');
  assert.equal(K.setCeil(L, 0, 4).cols[0].ceil, 4);
  assert.ok(K.levelText(up, 4).includes('\n3 0 crate\n1 2 mix\n12 0 crate\n'), 'written back in runs');
  const lowered = [16, 17, 18, 19, 20].reduce((l, c) => K.setHeight(l, c, 0, 'crate'), L);
  assert.ok(K.levelText(lowered, 4).includes('\n16 0 crate\n5 0 crate 6\n'), 'a wall taken down from under its barrier: the barrier stays');
  const flat = [16, 17, 18, 19, 20].reduce((l, c) => K.setCeil(l, c, 0), lowered);
  assert.ok(K.levelText(flat, 4).includes('\n21 0 crate\n4 3 mix\n'), '...and gone too: one street with the street before it');
});

test('saws and disco balls', () => {
  const L = K.parseLevel(small);
  const one = K.addSaw(L, 1.5);
  assert.deepEqual(one.saws, [1.5, 14.1, 22.5]);
  assert.equal(K.addSaw(one, 1.7), one, 'not on top of another');
  assert.equal(K.sawNear(one, 1.6), 0);
  assert.deepEqual(K.removeSaw(one, 0).saws, L.saws);
  assert.deepEqual(K.moveSaw(L, 0, 5).saws, [5, 22.5]);
  assert.ok(near(K.snapSaw(L, 15.8), 14.1 + K.SAW_ROW_GAP), 'near a saw’s row gap: blades back to back');
  assert.equal(K.snapSaw(L, 18.04), 18, 'elsewhere: on the decimetre');
  assert.equal(K.addDisco(L, 3, 2).discos[0].x, 3);
  assert.equal(K.removeDisco(L, 0).discos.length, 0);
  assert.deepEqual(K.moveDisco(L, 0, 20, 2).discos, [{ x: 20, y: 2 }]);
});

test('columns copied in, pasted and taken out: what’s after them moves along', () => {
  const L = K.parseLevel(small);
  const longer = K.duplicateCols(L, 16, 17);
  assert.equal(longer.cols.length, 57);
  assert.deepEqual(wall(longer.cols[18]), { h: 2, kind: 'hay', ceil: 6 });
  assert.ok(K.levelText(longer, 4).includes('\n7 2 hay 6\n'), 'the wall made longer, still one wall');
  assert.deepEqual(longer.saws, [15.3, 23.7], 'the saws past them, on');
  assert.ok(near(longer.length, 31.2), 'the finish line too');
  const clip = K.extractCols(L, 21, 24); // (the mix wall, and the saw on it)
  const pasted = K.insertCols(L, 2, clip);
  assert.deepEqual(wall(pasted.cols[2]), { h: 3, kind: 'mix', ceil: 0 });
  assert.deepEqual(pasted.saws, [2.7, 16.5, 24.9], 'its saw with it; the others on');
  assert.ok(near(pasted.length, 32.4));
  const roomy = K.setLength(L, 40);
  const shorter = K.deleteCols(roomy, 0, 3);
  assert.equal(shorter.cols.length, roomy.cols.length - 4);
  assert.ok(near(shorter.length, 37.6), 'the finish line back with them');
  assert.ok(near(shorter.saws[0], 14.1 - 2.4) && near(shorter.discos[0].x, 10.5 - 2.4));
  assert.equal(K.deleteCols(L, 0, 40), L, 'the course keeps 30 m');
});

test('the finish line: the course runs on to it', () => {
  const L = K.parseLevel(small);
  const far = K.setLength(L, 40);
  assert.equal(far.length, 40);
  assert.ok(K.courseLength(far) >= far.length - 1e-9);
  assert.equal(K.setLength(L, 10).length, K.MIN_LENGTH);
});

test('a gauntlet: placed with its door’s approach, moved, taken out, refitted', () => {
  const L = K.setLength(K.parseLevel(small), 120);
  const grid = { cols: 20, entryRow: 1, entryTop: 4, exitRow: 2 };
  const placed = K.placeGauntlet(K.addSaw(L, 25), 'tomb', grid, 30);
  const s = K.spans(placed)[0];
  assert.deepEqual([s.name, s.c0, s.c1], ['tomb', 30, 50]);
  assert.deepEqual(wall(placed.cols[29]), { h: 1, kind: 'crate', ceil: 4 }, 'the lintel, as low as its door');
  assert.deepEqual(wall(placed.cols[10]), { h: 1, kind: 'crate', ceil: 0 }, 'the forecourt, at its door’s height');
  assert.equal(placed.cols[50].h, 2, 'the porch, at its exit’s');
  assert.ok(!placed.saws.includes(25), 'what was there goes');
  assert.ok(K.levelText(placed, 4).includes('\n19 1 crate\n3 1 crate 4\n20 0 gauntlet tomb\n8 2 crate\n'));
  const moved = K.placeGauntlet(placed, 'tomb', grid, 100);
  assert.equal(K.spans(moved).length, 1);
  assert.equal(moved.cols[35].h, 0, 'where it was: street');
  assert.ok(moved.length >= (100 + 20 + 8) * K.U, 'the finish line past its porch');
  const wider = K.fitGauntlet(placed, 'tomb', 24);
  assert.equal(K.spans(wider)[0].c1, 54);
  assert.ok(near(wider.length, placed.length + 2.4));
  const out = K.removeGauntlet(placed, 'tomb');
  assert.equal(K.spans(out).length, 0);
  assert.deepEqual([out.cols[10].h, out.cols[29].ceil, out.cols[52].h], [0, 0, 0], 'taken out, its approach with it: street again');
  assert.equal(K.duplicateCols(placed, 35), placed, 'never inside a gauntlet');
  assert.equal(K.deleteCols(placed, 45, 55), placed);
  assert.equal(K.addSaw(placed, 35 * K.U), placed, 'no saw in its span');
  assert.equal(K.placeGauntlet(placed, 'other', grid, 40), placed, 'not on top of another');
});

test('where the play-test can start on the course: clear of the saws, short of the finish, not in a gauntlet', () => {
  const L = K.parseLevel(small);
  assert.equal(K.startOnCourse(L, 5.04), 5, 'on the decimetre');
  assert.equal(K.startOnCourse(L, 14.3), 15.2, 'off a saw, to its nearer side');
  assert.equal(K.startOnCourse(L, 13.9), 13, '...this side');
  assert.equal(K.startOnCourse(L, 400), 25, 'short of the finish');
  const placed = K.placeGauntlet(K.setLength(L, 120), 'tomb', { cols: 20, entryRow: 1, entryTop: 4, exitRow: 2 }, 30);
  assert.equal(K.startOnCourse(placed, 35 * K.U), null, 'in a gauntlet’s span: a cell of its grid instead');
});
