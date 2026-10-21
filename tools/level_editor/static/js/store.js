// What the editor's two views edit, shared: the campaign's levels (each a course, levels.js) and the maze gauntlets
// (each a grid, gauntlets.js), as saved and as edited. Which level lays which gauntlet is its course's say (a span of
// it), so a gauntlet moves between levels by editing them; its grid's width is its span's. Views say what they
// changed (emit) and hear what the other did (on).
import * as K from './course.js';
import * as G from './grid.js';

export const store = { levels: [], gauntlets: [] };

const listeners = new Map();
export function on(event, fn) { if (!listeners.has(event)) listeners.set(event, []); listeners.get(event).push(fn); }
export function emit(event, arg) { for (const fn of listeners.get(event) || []) fn(arg); }

export const levelNumbered = (n) => store.levels.find((lv) => lv.number === n) || null;
export const gauntletNamed = (name) => store.gauntlets.find((g) => g.name === name) || null;
// the level whose course lays it, as edited (none: it's in no level yet)
export const levelLaying = (name) => store.levels.find((lv) => lv.L.cols.some((k) => k.g === name)) || null;
// a level's gauntlet, as edited
export function gauntletOfLevel(lv) { const s = K.spans(lv.L)[0]; return s ? gauntletNamed(s.name) : null; }
// a grid's doors and size, as the level lays it (null: no grid, or no doors yet)
export function gridInfo(name) { const g = gauntletNamed(name); return g ? G.doors(g.rows) : null; }

// a level's course with every gauntlet's span as wide as its grid is now (a grid grown or shrunk moves what's after it)
export function fitted(L) {
  let out = L;
  for (const s of K.spans(L)) {
    const g = gauntletNamed(s.name);
    if (g) out = K.fitGauntlet(out, s.name, G.cols(g.rows));
  }
  return out;
}

export const levelDirty = (lv) => lv.text !== lv.saved.text;
export const gauntletDirty = (g) => !g.saved || !G.same(g.rows, g.saved.rows) || g.description.join('\n') !== g.saved.description.join('\n');
export const unsavedGrids = () => Object.fromEntries(store.gauntlets.filter(gauntletDirty).map((g) => [g.name, g.rows]));
