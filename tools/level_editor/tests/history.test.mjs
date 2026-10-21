// The level editor's history (static/js/history.js, describe.js), in Node (ctest "level_editor_js"): a timeline of
// states (edits recorded, undone, redone, jumped to; an edit after undoing drops what was undone; the oldest go past
// the cap; the saved state marked, the edits since it); and what each edit did, in words and where, for every tool of
// both views; a course's columns lined up so a run copied in reads as itself; what's changed since the save.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import * as H from '../static/js/history.js';
import * as D from '../static/js/describe.js';
import * as K from '../static/js/course.js';
import * as G from '../static/js/grid.js';

test('a timeline: record, undo, redo, jump, the branch undone dropped, the cap, the save', () => {
  let t = H.timeline('a', 'As saved', 0);
  t = H.record(t, 'b', 'one', null, 'edit', 1);
  t = H.record(t, 'c', 'two', null, 'edit', 2);
  assert.equal(H.current(t), 'c');
  assert.equal(H.record(t, 'c', 'same'), t, 'nothing changed: the same timeline');
  t = H.undo(t);
  assert.deepEqual([H.current(t), H.canRedo(t), H.nextRedo(t).label, H.nextUndo(t).label], ['b', true, 'two', 'one']);
  t = H.record(t, 'd', 'three');
  assert.deepEqual(t.states, ['a', 'b', 'd'], 'an edit after undoing drops what was undone');
  t = H.jump(t, 0);
  assert.equal(H.current(t), 'a');
  t = H.redo(H.redo(t));
  assert.equal(H.current(t), 'd');
  assert.deepEqual(H.sinceSaved(t).map((e) => e.label), ['three', 'one'], 'the edits since the save, newest first');
  t = H.markSaved(t);
  assert.deepEqual(H.sinceSaved(t), []);
  t = H.record(t, 'e', 'four');
  assert.deepEqual(H.sinceSaved(t).map((e) => e.label), ['four']);
  let big = H.timeline('0');
  for (let i = 1; i <= H.CAP + 5; i++) big = H.record(big, String(i), 'edit ' + i);
  assert.equal(big.states.length, H.CAP, 'the oldest go past the cap');
  assert.equal(big.saved, -1, '...the saved state with them');
  assert.equal(H.current(big), String(H.CAP + 5));
  assert.equal(H.ago(0, 5000), 'now');
  assert.equal(H.ago(0, 3 * 60000), '3 min');
});

test('lining columns up: a run copied in reads as itself', () => {
  assert.deepEqual(D.diffSeq(['a', 'b', 'c'], ['a', 'x', 'b', 'c']), [{ op: '=', n: 1 }, { op: '+', n: 1 }, { op: '=', n: 2 }]);
  assert.deepEqual(D.diffSeq(['a', 'b', 'c'], ['a', 'c']), [{ op: '=', n: 1 }, { op: '-', n: 1 }, { op: '=', n: 1 }]);
  assert.deepEqual(D.diffSeq(['a', 'b'], ['a', 'z']), [{ op: '=', n: 1 }, { op: '-', n: 1 }, { op: '+', n: 1 }]);
  const ops = D.diffSeq([...'kitten'], [...'sitting']);
  const kept = ops.filter((o) => o.op === '=').reduce((n, o) => n + o.n, 0);
  assert.equal(kept, 4, 'the longest run in common kept (i, t, t, n)');
});

const course = `length 60
difficulty 0 0.35
raids auto
course
40 0 crate
10 3 crate
50 0 crate
saws
30.3 45.3
discos
20 1.5
`;
test('what a course edit did, in words and where', () => {
  const L = K.parseLevel(course);
  const say = (kind, now, hint) => D.describeLevel(kind, L, now, hint).label;
  assert.equal(say('walls', [10, 11, 12].reduce((l, c) => K.setHeight(l, c, 2, 'crate'), L)), 'Walls 2 high · 3 columns at 6 m–7.8 m');
  assert.equal(say('walls', K.setHeight(L, 45, 0)), 'Walls down to the street · 1 column at 27 m');
  assert.equal(say('kind', K.setKind(L, 41, 'hay')), 'Hay · 1 column at 24.6 m');
  assert.equal(say('ceil', K.setCeil(L, 42, 7)), 'Barrier at 7 · 1 column at 25.2 m');
  assert.equal(say('saw-add', K.addSaw(L, 10)), 'Saw added at 10 m');
  assert.equal(say('saw-move', K.moveSaw(L, 0, 33)), 'Saw moved 30.3 m → 33 m');
  assert.equal(say('saw-remove', K.removeSaw(L, 1)), 'Saw removed at 45.3 m');
  assert.equal(say('disco-add', K.addDisco(L, 12, 2.5)), 'Disco ball added at 12 m, 2.5 m up');
  assert.equal(say('cols-in', K.duplicateCols(L, 40, 43), { c0: 44, c1: 47 }), '4 columns copied in at 26.4 m');
  assert.equal(say('cols-out', K.deleteCols(L, 5, 6), { c0: 5, c1: 6 }), '2 columns taken out at 3 m');
  assert.equal(say('finish', K.setLength(L, 70)), 'Finish line 60 m → 70 m');
  assert.equal(say('raids', { ...L, raids: 2 }), 'Raids auto → 2');
  assert.equal(say('notes', { ...L, notes: ['hi'] }), 'Description edited');
  const focus = D.describeLevel('walls', L, K.setHeight(L, 45, 0)).focus;
  assert.deepEqual(focus.map((x) => Math.round(x * 10) / 10), [27, 27.6], 'where: the column it changed');
});

test('since the save: columns lined up, saws and disco balls followed along', () => {
  const L = K.parseLevel(course);
  const now = K.addSaw(K.duplicateCols(L, 0, 3), 5); // (4 columns copied in at the start: everything after moves on)
  const d = D.diffCourse(L, now);
  assert.equal(d.inserted.length, 1);
  assert.equal(d.replaced.length, 0, 'nothing after the copy reads as changed');
  assert.deepEqual([d.saws.added, d.saws.removed], [[5], []], 'the saws that moved on with it: the same saws');
  assert.equal(d.discos.removed.length, 0);
  assert.equal(D.summarizeCourse(d), '4 columns put in, 1 saw added, the finish +2.4 m');
  const out = D.diffCourse(L, K.deleteCols(L, 10, 12));
  assert.deepEqual([out.removed.length, out.removed[0].n], [1, 3], '3 columns taken out');
  assert.ok(out.removed[0].at >= 10 && out.removed[0].at <= 30, '...somewhere in the flat run they came from (alike, a diff can\u2019t tell which)');
  assert.deepEqual([out.saws.removed, out.discos.removed], [[], []], '...the saws and the disco ball after them followed along');
});

test('a saw or disco ball put on or taken off: just that, never columns put in here and taken out there', () => {
  const L = K.parseLevel(course);
  let now = K.addDisco(L, 25.5, 3.5); // (on the wall)
  now = K.removeSaw(now, 0);
  now = [47, 48, 49].reduce((l, c) => K.setHeight(l, c, 4, 'crate'), now);
  const d = D.diffCourse(L, now);
  assert.deepEqual([d.inserted, d.removed], [[], []]);
  assert.deepEqual(d.replaced.map((r) => [r.c0, r.c1]), [[47, 49]], 'only the walls raised read as changed columns');
  assert.equal(D.summarizeCourse(d), '3 columns changed, 1 saw removed, 1 disco ball added');
  const copied = K.addDisco(K.duplicateCols(now, 20, 21), 3.3, 2.6); // (and two columns copied in a flat run, a disco ball before them)
  assert.equal(D.summarizeCourse(D.diffCourse(L, copied)), '3 columns changed, 2 columns put in, 1 saw removed, 2 disco balls added, the finish +1.2 m');
});

const room = ['########', '#......#', '#......#', '>......X', '########'];
test('what a grid edit did, in words and where', () => {
  const say = (kind, now, hint) => D.describeGrid(kind, room, now, hint).label;
  assert.equal(say('brush', G.setCell(G.setCell(room, 2, 2, '#'), 3, 2, '#')), 'Brush: 2 cells to stone');
  assert.equal(say('rect', G.fillRect(room, 1, 1, 3, 2, 'B')), 'Rect: 3 × 2 to bumper');
  assert.equal(say('door', G.moveDoor(room, '>', 2)), 'Entry door to row 2');
  assert.equal(say('cols-in', G.duplicateCols(room, 2, 3), { c0: 4, c1: 5 }), '2 columns copied in at col 4');
  assert.equal(say('cols-out', G.deleteCols(room, 2, 2), { c0: 2, c1: 2 }), '1 column taken out at col 2');
  const d = D.diffGrid(room, G.setCell(room, 2, 2, 'S'));
  assert.equal(d.cells.get('2,2'), '.', 'a changed cell: what it was');
  assert.deepEqual(D.gridFocus(d, room), { c0: 2, i0: 2, c1: 2, i1: 2 });
});
