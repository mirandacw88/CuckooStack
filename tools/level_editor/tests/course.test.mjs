// The level editor's course model (static/js/course.js), in Node (ctest "level_editor_js"): every level the game
// ships reads and writes back as it was (the game's own writer's text, two like walls side by side kept two); walls
// raised and lowered, their kinds, barriers kept off her head, a changed column joining its like neighbours; saws (in a
// row, a blade's gap apart), disco balls, wall lasers (set into walls' sides), lava and springs; bouncer walls (bumpers,
// gates) and their bounce zones; columns copied in, pasted and taken out, what's after them moved along;
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

test('floating blocks: read and written back, laid and taken off, never on her head, the floor she stands on', () => {
  const L = K.parseLevel(small.replace('16 0 crate\n', '10 0 crate\n6 0 hay 3 5\n'));
  assert.deepEqual({ ceil: L.cols[12].ceil, top: L.cols[12].top, kind: L.cols[12].kind }, { ceil: 3, top: 5, kind: 'hay' });
  assert.ok(K.levelText(L, 1).includes('\n6 0 hay 3 5\n'), 'its line: cells, height, kind, underside, top');
  assert.equal(K.floorAt(L, 12 * K.U, 0), 0, 'under it: the street');
  assert.equal(K.floorAt(L, 12 * K.U, 5), 5, 'up on it: its top');
  assert.equal(K.tallest(K.setBlock(L, 0, 8, 9)), 9, '(the tallest counts a block)');
  let B = K.setBlock(L, 2, 2, 4, 'mix');
  assert.deepEqual({ ceil: B.cols[2].ceil, top: B.cols[2].top, kind: B.cols[2].kind }, { ceil: 2, top: 4, kind: 'mix' }, 'over the street: of the kind picked');
  assert.equal(K.setBlock(B, 2, 2, 2), B, 'its top over its underside');
  assert.equal(K.setBlock(B, 17, 2, 5), B, 'its underside 1 over the wall at least (a wall 2 high there)');
  assert.notEqual(K.setBlock(B, 17, 3, 5), B, '...1 over it is enough: a block too low to duck under');
  assert.equal(K.setBlock(B, 2, 2, 61), B, 'its top up to 60');
  const off = K.setBlock(B, 2, 0, 0);
  assert.deepEqual({ ceil: off.cols[2].ceil, top: off.cols[2].top }, { ceil: 0, top: 0 }, 'taken off, its underside with it');
  const raised = K.setHeight(B, 2, 2, 'crate');
  assert.deepEqual({ ceil: raised.cols[2].ceil, top: raised.cols[2].top }, { ceil: 0, top: 0 }, 'a wall raised into it: it goes');
  assert.equal(K.setHeight(B, 2, 1, 'crate').cols[2].top, 4, '...one under its underside: it stays');
  assert.equal(K.setCeil(B, 2, 4), B, 'a block’s underside under its top');
  const J = K.setBlock(K.setBlock(L, 11, 3, 5, 'hay'), 10, 3, 5, 'hay');
  assert.equal(K.walls(J).filter((w) => w.top).length, 1, 'like blocks side by side: one wall');
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
  assert.ok(K.levelText(placed, 4).includes(`\n${K.FORECOURT} 1 crate\n3 1 crate 4\n20 0 gauntlet tomb\n8 2 crate\n`));
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

test('a gauntlet out high: a fair stair down off its porch to the course (the iPhone and iPad editor’s)', () => {
  let L = K.setLength(K.parseLevel(small), 200);
  const grid = { cols: 20, entryRow: 1, entryTop: 4, exitRow: 7 };
  const placed = K.placeGauntlet(L, 'tomb', grid, 30);
  const z = 30 + 20 + K.PORCH;
  assert.equal(placed.cols[z - 1].h, 7, 'its porch at its exit’s height');
  const steps = [];
  let h = 7, at = z;
  for (let i = z; i < placed.cols.length && h > 0; i++) {
    const k = placed.cols[i].h;
    if (k === h) continue;
    assert.ok(k < h && h - k <= 2, 'down 2 at the most a step');
    if (at !== z) assert.ok(i - at >= K.landingCells(2) - 1, 'each landing long enough to land and react on');
    steps.push(h - k); h = k; at = i;
  }
  assert.deepEqual(steps, [2, 2, 2, 1], 'a stair down from 7 to the street');
  const stair = K.walls(placed).filter((w) => !w.g && w.c0 >= z && [5, 3, 1].includes(w.h));
  assert.deepEqual(stair.map((w) => w.h), [5, 3, 1], 'one wall a step (its landing whole: the checker measures it so)');
  for (let k = 66; k < 80; k++) L.cols[k] = { ...L.cols[k], h: 6 };
  const tall = K.placeGauntlet(L, 'tomb', grid, 30);
  assert.deepEqual([tall.cols[z].h, tall.cols[66].h, tall.cols[80].h], [6, 6, 0], 'a step onto a wall it meets, ending there (from there, the course’s own)');
  L = K.setLength(K.parseLevel(small), 200);
  for (let k = 30; k < 33; k++) L.cols[k] = { ...L.cols[k], h: 1, kind: 'hay' };
  const onto = K.placeGauntlet(L, 'tomb', { cols: 20, entryRow: 1, entryTop: 4, exitRow: 2 }, 33 + K.FORECOURT + K.LINTEL);
  const run = K.walls(onto).find((w) => w.c0 === 30);
  assert.ok(run && run.h === 1 && run.kind === 'hay' && run.c1 > 33, 'its forecourt one wall with a like one just before it (its kind), not a short run of its own');
});

test('a stair down leaves no saw on a step’s edge or right after a drop (the checker finds it unfair); those on a flat landing stay', () => {
  let L = K.setLength(K.parseLevel(small), 200);
  let n = 0;
  for (let x = 36; x < 70; x += 2.1) { const m = K.addSaw(L, x); n += m === L ? 0 : 1; L = m; }
  const placed = K.placeGauntlet(L, 'tomb', { cols: 20, entryRow: 1, entryTop: 4, exitRow: 7 }, 30);
  assert.ok(n > 8 && placed.saws.length > 0 && placed.saws.length < n, 'some saws gone, some kept');
  for (const x of placed.saws) {
    const a = placed.cols[K.colAt(x - K.SAW_REACH - 0.01)], b = placed.cols[K.colAt(x + K.SAW_REACH + 0.01)];
    assert.equal(a.h, b.h, 'each on a flat');
  }
});

test('a gauntlet out low onto a wall too tall to climb: a stair up in front of it, a run-up before each climb (the iPhone and iPad editor’s)', () => {
  const L = K.setLength(K.parseLevel(small), 200);
  for (let k = 58; k < 80; k++) L.cols[k] = { ...L.cols[k], h: 9 };
  const grid = { cols: 20, entryRow: 1, entryTop: 4, exitRow: 1 };
  const placed = K.placeGauntlet(L, 'tomb', grid, 30);
  const z = 30 + 20 + K.PORCH;
  assert.equal(placed.cols[z - 1].h, 1, 'its porch at its exit’s height');
  const epc = 0.6 / (12.4 * 0.4 * 1.1);
  let b = Math.min(8, 1 + 8 * epc), h = 1, climbs = 0, i = z - K.PORCH;
  for (; i < placed.cols.length && h < 9; i++) {
    const k = placed.cols[i].h;
    if (k > h) { assert.ok(k - h <= 4, 'each climb 4 at the most'); assert.ok(b + 1e-6 >= k - h + 0.4, 'the meter holds it, with a margin'); b -= k - h; h = k; climbs++; }
    b = Math.min(8, b + epc);
  }
  assert.ok(h === 9 && climbs >= 2, 'up to the wall in two climbs or more');
  assert.ok(placed.cols[i - 1].cut, 'the wall itself whole');
  assert.ok(near(placed.length, L.length + (placed.cols.length - L.cols.length) * K.U) && placed.cols.length > L.cols.length, 'the course after it moved on');
  const low = K.parseLevel(small);
  for (let k = 58; k < 80; k++) low.cols[k] = { ...low.cols[k], h: 2 };
  assert.deepEqual(K.stairUp(K.setLength(low, 200).cols, z, 1), [], 'a wall 2 up, a climb the meter holds: no stair');
});

test('wall lasers: set into a wall’s or block’s side, read and written as the game does, snapped, moved, carried with their columns', () => {
  // (the walls: 16 street, 5 hay 2 high under a barrier at 6, 4 mix 3 high, street; 9.6 m: the hay's left face, 15 m:
  // the mix wall's right face)
  const text = small.replace(/\n$/, '') + '\nlasers\n9.6 1 0\n15 2 1\nlava\n3 4\n';
  const L = K.parseLevel(text);
  assert.deepEqual([L.lasers, L.lava], [[{ x: 9.6, row: 1, dir: 0 }, { x: 15, row: 2, dir: 1 }], [{ x: 3, len: 4 }]]);
  assert.equal(K.levelText(L, 4), text);
  assert.deepEqual(K.parseLevel(small + 'lasers\n9.6\n').lasers, [{ x: 9.6, row: 1, dir: 0 }], 'row 1, firing left: its defaults');
  assert.equal(K.levelText(K.removeLava(K.removeLaser(K.removeLaser(L, 0), 0), 0), 4), small, 'no section when there are none');
  // the cells it's set into, and fires along
  assert.deepEqual([K.solidCell(L, 16, 1), K.solidCell(L, 15, 1), K.solidCell(L, 18, 6), K.solidCell(L, 18, 4), K.solidCell(L, -1, 0)], [true, false, true, false, false]);
  assert.ok(K.laserFits(L, { x: 9.6, row: 0, dir: 0 }));
  assert.ok(!K.laserFits(L, { x: 9.6, row: 1, dir: 1 }), 'firing straight into its wall');
  assert.ok(!K.laserFits(L, { x: 9.7, row: 0, dir: 0 }), 'off a column’s edge');
  assert.ok(!K.laserFits(L, { x: 9.6, row: 3, dir: 0 }), 'no wall there to be set into');
  assert.deepEqual(K.laserReach(L, L.lasers[0]), [0, 9.6], 'left to the course’s start');
  assert.deepEqual(K.laserReach(L, L.lasers[1]), [15, 30.6], '26 cells at the most');
  assert.deepEqual(K.laserReach(L, { x: 12.6, row: 7, dir: 1 }), [12.6, 28.2], 'over the mix wall: 26 cells');
  // a click snaps to the nearest side within two column edges, at its row
  assert.deepEqual(K.faceNear(L, 9, 0.7), { x: 9.6, row: 1, dir: 0 });
  assert.equal(K.faceNear(L, 3, 0.7), null, 'no side near: none');
  assert.deepEqual(K.faceNear(L, 12.5, 30), { x: 12.6, row: 14, dir: 1 }, 'a barrier’s side, 14 rows up at most');
  assert.deepEqual(K.addLaser(L, 15.3, 0.4).lasers[1], { x: 15, row: 0, dir: 1 }, 'in order along, then up');
  assert.equal(K.addLaser(L, 15.1, 1.3), L, 'not on top of another');
  assert.equal(K.addLaser(L, 3, 1), L, 'nowhere to set it');
  const moved = K.moveLaser(L, 0, 15.2, 0.2);
  assert.deepEqual(moved.lasers, [{ x: 15, row: 0, dir: 1 }, { x: 15, row: 2, dir: 1 }], 'dragged to the side nearest');
  assert.equal(K.laserIndex(moved, { x: 15, row: 2, dir: 1 }), 1);
  assert.equal(K.laserNear(L, 15.2), 1);
  assert.equal(K.laserNear(L, 15.2, 0.45, 1), -1, 'at its row');
  // its wall edited from under it: it goes; its columns copied, put in and taken out: it goes with them
  assert.deepEqual(K.setHeight(L, 24, 0).lasers, [L.lasers[0]]);
  const longer = K.duplicateCols(L, 0, 1);
  assert.deepEqual(longer.lasers.map((z) => z.x), [10.8, 16.2]);
  const clip = K.extractCols(L, 21, 24);
  assert.deepEqual(clip.lasers, [{ x: 2.4, row: 2, dir: 1 }], 'the mix wall’s, in its right face');
  assert.deepEqual(K.insertCols(L, 40, clip).lasers.at(-1), { x: 26.4, row: 2, dir: 1 });
  const shorter = K.deleteCols(K.setLength(L, 40), 5, 6);
  assert.deepEqual([shorter.lava, shorter.lasers.map((z) => z.x)], [[], [8.4, 13.8]]);
  assert.deepEqual(K.deleteCols(K.setLength(L, 40), 16, 16).lasers.map((z) => z.x), [14.4], 'its wall taken out: it goes');
  const placed = K.placeGauntlet(K.setLength(L, 120), 'g', { cols: 20, entryRow: 0, entryTop: 4, exitRow: 0 }, 30);
  assert.ok(!placed.lava.length && !placed.lasers.length, 'what was there goes');
  assert.equal(K.addLaser(placed, 35 * K.U, 0.7), placed, 'no laser in its span');
  assert.equal(K.addLaser(placed, 30 * K.U - 0.1, 0.7), placed, 'nor in its face: a maze’s lasers are its grid’s');
});

test('lava: placed, stretched, kept off gauntlets and the course’s end', () => {
  const L = K.parseLevel(small.replace(/\n$/, '') + '\nlava\n3 4\n');
  assert.equal(K.addLava(L, 4.2), L, 'not over other lava');
  assert.deepEqual(K.addLava(L, 6, 20).lava[1], { x: 6, len: 8 }, '8 cells at most');
  assert.deepEqual(K.moveLava(L, 0, 3, 2).lava, [{ x: 3, len: 2 }], 'stretched by its end');
  assert.equal(K.lavaOver(L, 5.3), 0);
  assert.equal(K.addLava(L, 32.4, 3), L, 'short of the course’s end');
  assert.equal(K.surfaceAt(L, 13.2), 3, 'on the mix wall: its top');
  assert.ok(near(K.duplicateCols(L, 0, 1).lava[0].x, 3 + 2 * K.U));
  const placed = K.placeGauntlet(K.setLength(L, 120), 'g', { cols: 20, entryRow: 0, entryTop: 4, exitRow: 0 }, 30);
  assert.equal(K.addLava(placed, 28 * K.U, 3), placed, 'no lava running into it');
});

test('springs: read and written as the game does, placed on the decimetre, moved, lifted, kept apart and off gauntlets', () => {
  const text = small.replace(/\n$/, '') + '\nlava\n3 4\nsprings\n6 6\n9.9 9\n';
  const L = K.parseLevel(text);
  assert.deepEqual(L.springs, [{ x: 6, power: 6 }, { x: 9.9, power: 9 }]);
  assert.equal(K.levelText(L, 4), text, 'after lava, as the game writes them');
  assert.deepEqual(K.parseLevel(small + 'springs\n4.5\n').springs, [{ x: 4.5, power: 6 }], '6 rows of lift: its default');
  assert.equal(K.levelText(K.removeSpring(K.removeSpring(L, 0), 0), 4), small.replace(/\n$/, '') + '\nlava\n3 4\n', 'no section when there are none');
  // placed: on the decimetre, 2 to 12 rows, 1.2 m from another, on the course
  assert.deepEqual(K.addSpring(L, 20.04).springs[2], { x: 20, power: 6 });
  assert.deepEqual(K.addSpring(L, 20, 30).springs[2], { x: 20, power: 12 }, '12 rows at most');
  assert.deepEqual(K.addSpring(L, 20, 0).springs[2], { x: 20, power: 2 }, '2 at least');
  assert.deepEqual(K.addSpring(L, 1.2).springs.map((p) => p.x), [1.2, 6, 9.9], 'in order along');
  assert.equal(K.addSpring(L, 7.1), L, 'not within 1.2 m of another');
  assert.ok(K.addSpring(L, 7.2) !== L, '...1.2 m is fine');
  assert.equal(K.addSpring(L, 32.8), L, 'short of the course’s end (33 m: its pad’s half on it)');
  assert.equal(K.springNear(L, 9.7), 1);
  // moved, lifted
  assert.deepEqual(K.moveSpring(L, 1, 20.03).springs, [{ x: 6, power: 6 }, { x: 20, power: 9 }]);
  assert.equal(K.moveSpring(L, 1, 6.5), L, 'not onto another');
  assert.deepEqual(K.moveSpring(L, 0, 25).springs.map((p) => p.x), [9.9, 25], 'sorted along after a move');
  assert.deepEqual(K.setSpringPower(L, 1, 4.4).springs[1], { x: 9.9, power: 4 });
  assert.equal(K.setSpringPower(L, 1, 9), L, 'unchanged: the same level');
  // its columns copied, put in and taken out: it goes with them
  assert.deepEqual(K.duplicateCols(L, 0, 1).springs.map((p) => p.x), [7.2, 11.1]);
  const clip = K.extractCols(L, 9, 12);
  assert.deepEqual(clip.springs, [{ x: 0.6, power: 6 }]);
  assert.deepEqual(K.insertCols(L, 40, clip).springs.at(-1), { x: 24.6, power: 6 }, 'pasted where it’s put in');
  assert.deepEqual(K.insertCols(L, 0, clip).springs.map((p) => p.x), [0.6, 8.4, 12.3], 'what’s after moves on');
  const shorter = K.deleteCols(K.setLength(L, 40), 10, 10);
  assert.deepEqual(shorter.springs, [{ x: 9.3, power: 9 }], 'taken out with its column; what’s after moves back');
  assert.deepEqual(K.deleteCols(K.setLength(L, 40), 2, 3).springs.map((p) => p.x), [4.8, 8.7]);
  // a gauntlet: what was in its footprint goes; none in its span or within 0.3 m of it; moved along when it's refitted
  const placed = K.placeGauntlet(K.addSpring(K.setLength(L, 120), 60), 'g', { cols: 20, entryRow: 0, entryTop: 4, exitRow: 0 }, 30);
  assert.deepEqual(placed.springs.map((p) => p.x), [60]);
  assert.equal(K.addSpring(placed, 35 * K.U), placed, 'not in its span');
  assert.equal(K.addSpring(placed, 30 * K.U - 0.2), placed, 'nor at its door');
  assert.equal(K.addSpring(placed, 50 * K.U + 0.2), placed, 'nor at its exit');
  assert.deepEqual(K.fitGauntlet(placed, 'g', 24).springs.map((p) => p.x), [62.4], 'on with what’s after it');
  // the play-test's start: not on a spring
  assert.equal(K.startOnCourse(L, 6.2), 7.1, 'off it, to its nearer side');
});

test('bouncer walls: bumpers and gates read and written as the game does, never first, never under a barrier or block, in bounce zones', () => {
  const text = small.replace('4 3 mix\n', '4 3 mix\n2 4 bumper\n6 0 crate\n1 3 gate>\n1 3 gate<\n');
  const L = K.parseLevel(text);
  assert.deepEqual(wall(L.cols[25]), { h: 4, kind: 'bumper', ceil: 0 });
  assert.equal(K.levelText(L, 4), text, 'written back as it was');
  assert.ok(K.isBouncer(L.cols[33]) && K.isBouncer(L.cols[34]) && !K.isBouncer(L.cols[24]));
  // built: 1 high at least; down to the street, crates; never column 0 (the course's first wall)
  const up = K.setBouncer(L, 40, 2, 'gate>');
  assert.deepEqual(wall(up.cols[40]), { h: 2, kind: 'gate>', ceil: 0 });
  assert.ok(K.levelText(up, 4).includes('\n5 0 crate\n1 2 gate>\n'), 'a wall of its own');
  assert.equal(K.setBouncer(L, 0, 3, 'bumper'), L, 'never the first wall');
  assert.equal(K.setKind(L, 0, 'bumper'), L);
  assert.deepEqual(wall(K.setBouncer(L, 25, 0, 'bumper').cols[25]), { h: 0, kind: 'crate', ceil: 0 }, 'down to the street: crates');
  assert.deepEqual(wall(K.setHeight(L, 33, 0).cols[33]), { h: 0, kind: 'crate', ceil: 0 }, '...by the Walls tool too');
  assert.equal(K.setHeight(L, 25, 6, 'crate').cols[25].kind, 'bumper', 'raised, it keeps its kind');
  assert.equal(K.setHeight(L, 3, 2, 'bumper').cols[3].kind, 'crate', 'the Walls tool never makes one');
  assert.equal(K.setBouncer(L, 999, 2, 'bumper'), L);
  assert.equal(K.setBouncer(L, 40, 2, 'brick'), L);
  // a barrier or block: the bouncer takes it off; never put on one
  const barred = K.setBouncer(L, 16, 2, 'bumper');
  assert.deepEqual(wall(barred.cols[16]), { h: 2, kind: 'bumper', ceil: 0 }, 'the barrier over it goes');
  assert.equal(K.setCeil(L, 25, 7), L, 'no barrier over a bumper');
  assert.equal(K.setBlock(L, 33, 5, 7), L, 'no block over a gate');
  assert.equal(K.setKind(L, 16, 'bumper'), L, 'nor one made under a barrier');
  assert.equal(K.setKind(L, 3, 'gate<'), L, 'nor out of the street');
  assert.equal(K.setKind(L, 21, 'gate<').cols[21].kind, 'gate<');
  assert.equal(K.setKind(L, 25, 'hay').cols[25].kind, 'hay', 'back to a wall');
  // the bounce zones: from the column before the first to the last, bouncers 40 cells apart at most one zone
  assert.deepEqual(K.bounceZones(L), [{ c0: 24, c1: 35 }]);
  assert.deepEqual(K.zoneAt(L, 30), { c0: 24, c1: 35 });
  assert.equal(K.zoneAt(L, 35), null);
  const far = K.setLength(L, 60);
  assert.deepEqual(K.bounceZones(K.setBouncer(far, 75, 1, 'bumper')), [{ c0: 24, c1: 76 }], '40 apart: one zone');
  assert.deepEqual(K.bounceZones(K.setBouncer(far, 76, 1, 'bumper')), [{ c0: 24, c1: 35 }, { c0: 75, c1: 77 }], '41: two');
  // columns: copied and taken out with their kinds; never one left (or put) first
  const copied = K.duplicateCols(L, 25, 26);
  assert.ok(K.levelText(copied, 4).includes('\n4 4 bumper\n'), 'copied in after itself: one wall with it');
  const clip = K.extractCols(L, 33, 34);
  assert.deepEqual(clip.cols.map((k) => k.kind), ['gate>', 'gate<']);
  assert.deepEqual(K.insertCols(L, 2, clip).cols.slice(2, 4).map((k) => k.kind), ['gate>', 'gate<']);
  assert.equal(K.insertCols(L, 0, clip), L, 'never put in first');
  const lead = K.setBouncer(far, 1, 2, 'bumper');
  assert.notEqual(K.deleteCols(lead, 0, 1), lead, 'with it: fine');
  assert.equal(K.deleteCols(lead, 0, 0), lead, 'never left first');
  assert.equal(K.deleteCols(far, 25, 26).cols[25].kind, 'crate', 'taken out with its columns');
  // a gauntlet's forecourt never takes a bouncer's kind
  const placed = K.placeGauntlet(K.setLength(K.setBouncer(L, 40, 1, 'bumper'), 120), 'g', { cols: 20, entryRow: 1, entryTop: 4, exitRow: 0 }, 64);
  assert.equal(placed.cols[41].kind, 'crate');
});

test('a section the editor doesn’t know (a newer hazard of the game’s): kept as it is', () => {
  const text = small.replace(/\n$/, '') + '\nbeacons\n12 3 2.5\n18.2 4 1\n';
  const L = K.parseLevel(text);
  assert.deepEqual(L.extra, ['beacons', '12 3 2.5', '18.2 4 1']);
  assert.equal(K.levelText(L, 4), text, 'written back after the others');
  assert.equal(K.levelText(K.addSaw(L, 3), 4).endsWith('beacons\n12 3 2.5\n18.2 4 1\n'), true, 'through an edit');
});

test('a gauntlet’s approach as the game lays it (useRules: level_check --registry-json)', () => {
  const L = K.setLength(K.parseLevel(small), 120);
  const grid = { cols: 20, entryRow: 1, entryTop: 4, exitRow: 2 };
  assert.equal(K.FORECOURT + K.LINTEL, 23, 'till the server answers: the game’s forecourt, its lintel among it');
  try {
    K.useRules({ ok: true, cell: 0.6, lintel: 4, porch: 6, bodyRows: 2, budgetCap: 8, forecourt: 26 });
    const placed = K.placeGauntlet(L, 'tomb', grid, 30);
    assert.ok(K.levelText(placed, 4).includes('\n22 1 crate\n4 1 crate 4\n20 0 gauntlet tomb\n6 2 crate\n'), 'its forecourt, lintel and porch: the game’s');
    assert.deepEqual(K.gauntletFootprint(grid, 30), { a: 4, c: 30, z: 56 });
    K.useRules({ ok: true, cell: 0.6, lintel: 3, porch: 8, bodyRows: 2, budgetCap: 1, forecourt: 23 });
    assert.equal(K.placeGauntlet(L, 'tomb', grid, 30).cols[29].ceil, 0, 'a door with room under it for a full stack: no lintel');
    K.useRules({ ok: false, error: 'not built' });
    assert.equal(K.BUDGET_CAP, 1, 'the server’s error: the rules as they were');
  } finally {
    K.useRules({ ok: true, lintel: 3, porch: 8, bodyRows: 2, budgetCap: 8, forecourt: 23 });
  }
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

test('a gauntlet renamed in a level’s text: its course line alone, its spacing and note kept (renameGauntletIn)', () => {
  const text = '; the tomb level\ncourse\n30 1 crate\n168  0 gauntlet tomb ; its maze\n40 0 gauntlet tombs\nsaws\n';
  assert.equal(K.renameIn(text, 'tomb', 'crypt'), '; the tomb level\ncourse\n30 1 crate\n168  0 gauntlet crypt ; its maze\n40 0 gauntlet tombs\nsaws\n');
  assert.equal(K.renameIn(text, 'nope', 'crypt'), text);
});

test('a super hard level’s hard line (the generator’s density 8 to 10): read and written back as it was', () => {
  const text = 'length 60\ndifficulty 0 0.35\nraids auto\nhard 10\ncourse\n100 0 crate\nsaws\ndiscos\n';
  const L = K.parseLevel(text);
  assert.equal(L.hard, 10);
  assert.equal(K.levelText(L, 1), text);
  assert.ok(!K.levelText(K.parseLevel(text.replace('hard 10\n', '')), 1).includes('hard'));
});
