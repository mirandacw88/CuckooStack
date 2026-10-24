// The level editor's grid model (static/js/grid.js), in Node (ctest "level_editor_js": node --test): painting keeps
// the doors; doors move with their openings; columns and rows copied in and taken out; blocks lifted, flipped,
// pasted, and what they leave filled from their sides; the notes; the changed cells; the doors a level lays it by; a
// new gauntlet's first room.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import * as G from '../static/js/grid.js';

const room = [
  '##########',
  '#........#',
  '#........#',
  '>........X',
  '###S######',
];

test('painting a cell, a rectangle: never over a door', () => {
  assert.equal(G.setCell(room, 2, 1, 'B')[1], '#.B......#');
  assert.equal(G.setCell(room, 0, 3, '.'), room, 'the entry stays');
  assert.equal(G.setCell(room, 2, 1, '.'), room, 'nothing changed: the same rows');
  const filled = G.fillRect(room, 0, 1, 9, 3, '#');
  assert.deepEqual(filled.slice(1, 4), ['##########', '##########', '>########X']);
});

test('a door moves to a row, its opening as tall as the passage it opens onto', () => {
  const taller = ['##########', '#........#', '#........#', '#........#', '>........X', '##########'];
  const moved = G.moveDoor(taller, '>', 2);
  assert.deepEqual(moved.map((r) => r[0]), ['#', '.', '>', '#', '#', '#']);
  assert.equal(G.findDoor(moved, '>').i, 2);
  assert.deepEqual(G.moveDoor(taller, 'X', 3).map((r) => r[9]), ['#', '.', '.', 'X', '#', '#']);
  assert.equal(G.moveDoor(taller, '>', 0), taller, 'not the top row');
});

test('columns copied in after themselves, and taken out; never the doors’', () => {
  const wider = G.duplicateCols(room, 3);
  assert.equal(G.cols(wider), 11);
  assert.equal(wider[4], '###SS######');
  assert.equal(G.duplicateCols(room, 0)[3], '>.........X', 'the door column: the one next to it copied');
  const narrower = G.deleteCols(room, 3, 4);
  assert.equal(narrower[4], '#########'.slice(0, 8));
  assert.equal(G.deleteCols(room, 9)[3], room[3], 'the exit’s column stays');
  assert.equal(G.deleteCols(room, 1, 8), room, 'never under 4 columns');
});

test('rows copied in above themselves (everything over them up a row), and taken out; never a door’s', () => {
  const taller = G.duplicateRows(room, 1);
  assert.equal(taller.length, 6);
  assert.deepEqual(taller.slice(0, 3), ['##########', '#........#', '#........#']);
  assert.equal(G.findDoor(taller, '>').i, 4, 'the door keeps its row from the street');
  assert.equal(G.duplicateRows(room, 3).filter((r) => r.includes('>')).length, 1, 'a door is never copied');
  assert.equal(G.deleteRows(room, 3), room, 'a door’s row stays');
  assert.equal(G.deleteRows(room, 1).length, 4);
});

test('a block lifted leaves what its sides have; flipped, its gates turn round; pasted, the doors stay', () => {
  const corridor = ['##########', '#...##...#', '>..R..L..X', '###S######'];
  const block = G.extract(corridor, 3, 2, 4, 3);
  assert.deepEqual(block, [['R', '.'], ['S', '#']]);
  assert.deepEqual(G.flip(block), [['.', 'L'], ['#', 'S']]);
  const left = G.vacate(corridor, 3, 2, 4, 3);
  assert.equal(left[2], '>........X'.slice(0, 3) + '..' + 'L..X'.padStart(5, '.'));
  assert.equal(left[3], '##########', 'a blade lifted from a floor leaves floor');
  const pasted = G.paste(corridor, [['B', 'B']], -1, 2);
  assert.equal(pasted[2][0], '>', 'the entry stays under a paste');
  assert.equal(pasted[2][1], '.');
  assert.deepEqual(G.extract(corridor, 0, 2, 1, 2), [[null, '.']], 'a door never lifts');
});

test('wall lasers: stone with one in its left or right face, its beam along its row through the air', () => {
  const rows = ['#####', '(...)', '#..#)', '>...X', '#####'];
  assert.deepEqual(G.beamCells(rows), [
    { c: 0, i: 1, d: -1, c0: 0, c1: -1 },  // (firing left off the grid: no beam)
    { c: 4, i: 1, d: 1, c0: 5, c1: 4 },
    { c: 4, i: 2, d: 1, c0: 5, c1: 4 },
  ]);
  assert.deepEqual(G.beamCells(['.)..#', '..(.#']), [{ c: 1, i: 0, d: 1, c0: 2, c1: 3 }, { c: 2, i: 1, d: -1, c0: 0, c1: 1 }]);
  assert.ok(G.SOLID.has('(') && G.SOLID.has(')') && !G.CELL['='], 'solid as stone; the old beam cell is gone');
  assert.deepEqual(G.flip([['(', '.', '#']]), [['#', '.', ')']], 'flipped, they turn round');
  assert.deepEqual([G.CELL['('].key, G.CELL[')'].key, G.CELL['~'].key], ['[', ']', '=']);
  assert.equal(G.vacate(['#(..#', '#(..#'], 2, 0, 3, 1)[0], '#(##' + '#', 'a laser beside a lifted block leaves plain stone');
});

test('notes: blades side by side, a hole one wide; and the cells changed', () => {
  const blades = ['##########', '#........#', '>........X', '##SS#.####', '##########'];
  const notes = G.lints(blades);
  assert.equal(notes.length, 2);
  assert.ok(notes[0].text.startsWith('Blades side by side'));
  assert.ok(notes[1].text.startsWith('A hole one cell wide'));
  assert.deepEqual([...G.changed(G.setCell(room, 2, 1, 'B'), room)], ['2,1']);
  assert.equal(G.changed(G.duplicateCols(room, 2), room), null, 'another size: no cell map');
});

test('what a level lays a grid by: its doors; a new gauntlet’s plain room', () => {
  assert.deepEqual(G.doors(room), { cols: 10, rows: 5, entryRow: 1, entryTop: 2, exitRow: 1 }, 'the door’s opening: up the left edge while air');
  assert.equal(G.doors(room.map((r) => r.replace('>', '#'))), null, 'no door, nothing to lay it by');
  const t = G.template(30, 8);
  assert.deepEqual([t.length, G.cols(t)], [8, 30]);
  assert.deepEqual(G.doors(t), { cols: 30, rows: 8, entryRow: 1, entryTop: 4, exitRow: 1 }, 'its doors a row up, 3 high: a lintel’s height');
  assert.equal(t[0], '#'.repeat(30), 'its top closed');
  assert.equal(G.lints(t).length, 0);
});

test('where she can start in a maze: on a floor, room over her, never in a door’s column', () => {
  const g = ['##########', '#........#', '#........#', '#..##....#', '#........#', '>........X', '####S#####'];
  assert.deepEqual(G.standable(g, 2, 1), { c: 2, i: 5, b: 1 }, 'clicked in the air: where she lands, down the column');
  assert.deepEqual(G.standable(g, 3, 1), { c: 3, i: 2, b: 4 }, 'on a block, room over her');
  assert.equal(G.standable(['##########', '#........#', '#..##....#', '>........X', '##########'], 3, 1), null, 'under the roof: no room for her body');
  assert.equal(G.standable(g, 4, 5), null, 'on a blade: it would get her');
  assert.equal(G.standable(g, 3, 3), null, 'in a wall');
  assert.equal(G.standable(g, 0, 5), null, 'never a door’s column');
});

test('a spring: a cell with a key and a tip, solid, lifted as stone, and never a place to start', () => {
  const spring = G.CELLS.find((c) => c.ch === '^');
  assert.ok(spring && spring.name === 'Spring' && spring.key === '\\' && spring.tip, 'it is in the palette');
  assert.ok(G.SOLID.has('^'), 'solid: a floor');
  const floor = ['##########', '#........#', '#........#', '>........X', '###^######'];
  assert.equal(G.setCell(floor, 5, 4, '^')[4], '###^#^####', 'painted like any cell');
  assert.equal(G.standable(floor, 3, 3), null, 'on a spring she would be launched: no place to start');
  assert.equal(G.vacate(['#^.#'], 2, 0, 2, 0)[0], '#^##', 'lifted from beside a spring it leaves stone, never a second spring');
  assert.equal(G.vacate(['#^.#'], 1, 0, 1, 0)[0], '##.#', 'and a spring lifted itself leaves what its neighbour has: stone');
});
