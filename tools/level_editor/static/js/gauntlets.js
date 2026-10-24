// The level editor's gauntlet view: tune a maze gauntlet's grid with the game's own solver checking every edit
// (build/gauntlet_check, as the level laying it plays it), and the real game's bot playing it a moment after; replay
// the bot's run move by move; play it in the desktop game. A new gauntlet starts here, from a plain room.
import * as G from './grid.js';
import { GridView, KIND, C } from './view.js';
import { $, el, esc, titled, clamp, api, toast, local } from './util.js';
import { askText } from './dialog.js';
import { store, emit, levelLaying, unsavedGrids, gauntletDirty } from './store.js';
import * as H from './history.js';
import * as D from './describe.js';
import { renderHistory } from './historycard.js';
import { TIPS, CELL_TIP, setTip } from './tips.js';
import { showAt, hide as hideTip } from './tooltip.js';
import { GAUNTLET_TOOLS, GAUNTLET_OVERLAYS } from './tools.js';
import { setStart, clearStart, startOf, startLabel, gauntletRenamed } from './levels.js';

const key = (rows) => rows.join('\n');

const RATES = [1, 1.25, 1.4];          // the meter refilling this many times slower than the level's (the checker's runs)
const SPEEDS = [0.25, 0.5, 1];         // the replay, against the game's own pace
const NEED_MISSES = 15;                // (Gauntlet_test: every gauntlet in a level's near misses)
// ...and the checklist bends for a "; hard N" maze (N 6 to 10, GauntletReady): 15 + (N - 5) near misses, and the meter's spare
// 1.25 - 0.05 (N - 5) (down to 1.0 at 10): the report says (wantMisses, the second meter run's rate)
const needOf = (ch) => (ch && ch.wantMisses) || NEED_MISSES;
const rateOf = (g, i) => { const m = g && g.check && g.check.ok && g.check.meter && g.check.meter[i]; return m && m.rate ? +m.rate.toFixed(2) : RATES[i]; };
const TOOLS = GAUNTLET_TOOLS, OVERLAYS = GAUNTLET_OVERLAYS; // (tools.js)

const S = {
  cur: null,
  tool: 'brush', primary: '.', secondary: '#',
  overlays: { path: true, misses: true, reach: false, changes: true, grid: true },
  rate: 0, hover: null, preview: null, selection: null, clipboard: null, pasting: null, drag: null, space: false, active: false,
  focus: null, cellTip: null, notesBefore: null,
};
const R = { step: 0, playing: false, speed: 1, acc: 0, last: 0 };

const board = $('board'), boardWrap = $('boardWrap');
const view = new GridView(board, $('minimap'));

// ---- gauntlets: what's saved, what's edited, what the checker last said about it
export function gauntlet(g) {
  return { name: g.name, rows: g.rows, description: g.description, saved: g.saved === null ? null : { rows: g.rows, description: g.description },
    hist: H.timeline(stateOf(g.rows, g.description), g.saved === null ? 'A new gauntlet: a plain room' : 'As saved'),
    check: null, checkKey: '', game: null, gameKey: '', gameBusy: false, lints: [], changed: null, was: null, view: null };
}
// a grid's state on its history: its rows, and its description after a line no row can be
const SEP = '\n@@\n';
const stateOf = (rows, description) => rows.join('\n') + SEP + description.join('\n');
function fromState(text) { const i = text.indexOf(SEP); return { rows: text.slice(0, i).split('\n'), description: text.slice(i + SEP.length).split('\n') }; }
const cur = () => S.cur;
const levelOf = (g) => levelLaying(g.name);
const levelNo = (g) => { const lv = levelOf(g); return lv ? lv.number : 0; };
// what a check was of: the rows, and the level laying them (its text: the course before its door, where it is)
const checkKeyOf = (g) => { const lv = levelOf(g); return key(g.rows) + '\n@' + (lv ? lv.number + '\n' + lv.text : ''); };
const fresh = (g) => !!g.check && g.checkKey === checkKeyOf(g);
const gameFresh = (g) => g.gameKey === checkKeyOf(g) && !g.gameBusy;
const run = (g = cur()) => (g && g.check && g.check.ok && g.check.meter ? g.check.meter[S.rate] : null);

// unsaved edits survive a reload (this browser only): kept against the grid they started from
const draftKey = (g) => 'cuckoo-gauntlet-draft:' + g.name;
const baseOf = (g) => (g.saved ? key(g.saved.rows) + '\n--\n' + g.saved.description.join('\n') : '(new)');
export function storeDraft(g) {
  local.set(draftKey(g), gauntletDirty(g) ? { rows: g.rows, description: g.description, base: baseOf(g) } : null);
  local.set('cuckoo-gauntlet-new', store.gauntlets.filter((x) => !x.saved).map((x) => x.name));
}
export function restoreDraft(g) {
  const d = local.get(draftKey(g));
  if (d && d.base === baseOf(g) && Array.isArray(d.rows) && Array.isArray(d.description)) {
    g.rows = d.rows; g.description = d.description;
    if (g.saved) g.hist = H.record(g.hist, stateOf(g.rows, g.description), 'Unsaved edits, kept in this browser', null, 'draft');
    else g.hist = H.timeline(stateOf(g.rows, g.description), 'A new gauntlet, kept in this browser');
    return true;
  }
  return false;
}
// its history from this browser (history.js): kept if it grew from this saved grid and ends where the grid is now
export function restoreHistory(g, kept) {
  if (!kept || kept.base !== baseOf(g) || !kept.t || !Array.isArray(kept.t.states) || kept.t.states[kept.t.cursor] !== stateOf(g.rows, g.description)) return false;
  g.hist = kept.t;
  return true;
}
const keepHistory = (g) => H.keep('gauntlet:' + g.name, { base: baseOf(g), t: g.hist });
// gauntlets made here and not saved yet (their drafts)
export function restoreNew() {
  const out = [];
  for (const name of local.get('cuckoo-gauntlet-new', [])) {
    const d = local.get('cuckoo-gauntlet-draft:' + name);
    if (d && d.base === '(new)' && Array.isArray(d.rows) && !store.gauntlets.some((g) => g.name === name)) out.push(gauntlet({ name, rows: d.rows, description: d.description || [], saved: null }));
  }
  return out;
}

// ---- edits
function diffSaved(g) { // (what's changed since the save: its cells, and what each was)
  const d = g.saved ? D.diffGrid(g.saved.rows, g.rows) : null;
  g.was = d && !d.size ? d.cells : null;
  g.changed = g.was ? new Set(g.was.keys()) : null;
  g.diff = d;
}
function refresh(g, { live = false } = {}) { // (after any change to its rows)
  g.lints = G.lints(g.rows);
  diffSaved(g);
  if (!live) { storeDraft(g); keepHistory(g); }
  emit('grid', g); // (its level refits its span, and checks again)
  scheduleCheck(g);
  scheduleGame(g);
  if (live) return redraw();
  renderTabs(); emit('top'); renderPanels(); redraw();
}
// An edit: the grid's new rows on its history, with what it did (kind: the tool's; hint: the columns or rows it
// worked on, describe.js)
export function commit(rows, kind = 'edit', { snapshot = null, hint = null } = {}) {
  const g = cur();
  if (!g || G.same(rows, snapshot || g.rows)) { if (g && rows !== g.rows) { g.rows = rows; redraw(); } return false; }
  const { label, focus } = D.describeGrid(kind, snapshot || g.rows, rows, hint);
  g.hist = H.record(g.hist, stateOf(rows, g.description), label, focus, kind);
  g.rows = rows;
  refresh(g);
  return true;
}
function goTo(g, t) { // (along its history)
  if (t === g.hist) return;
  g.hist = t;
  const st = fromState(H.current(t));
  g.rows = st.rows; g.description = st.description;
  if (g === cur()) { S.selection = null; S.pasting = null; S.focus = null; }
  refresh(g);
}
export function undo() { const g = cur(); if (g && H.canUndo(g.hist)) goTo(g, H.undo(g.hist)); }
export function redo() { const g = cur(); if (g && H.canRedo(g.hist)) goTo(g, H.redo(g.hist)); }
export function jumpTo(g, i) { goTo(g, H.jump(g.hist, i)); }
// back to the saved grid; one never saved, thrown away (out of the level laying it). The shell asks first: Revert
// lists every unsaved edit. Undo brings a saved grid's edits back
export function revertGauntlet(g) {
  if (!gauntletDirty(g)) return;
  if (!g.saved) {
    emit('discard-gauntlet', g);
    store.gauntlets.splice(store.gauntlets.indexOf(g), 1);
    local.set('cuckoo-gauntlet-new', store.gauntlets.filter((x) => !x.saved).map((x) => x.name));
    local.set(draftKey(g), null);
    if (S.cur === g) S.cur = null;
    renderTabs();
    return;
  }
  g.hist = H.record(g.hist, stateOf(g.saved.rows, g.saved.description), 'Back to the save', D.gridFocus(D.diffGrid(g.rows, g.saved.rows), g.saved.rows), 'revert');
  g.rows = g.saved.rows; g.description = g.saved.description.slice();
  if (g === cur()) { S.selection = null; S.pasting = null; }
  refresh(g);
}
// what's changed since the save, in a few words (the Revert list)
export function changeSummary(g) {
  if (!g.saved) return 'new, never saved: it goes for good';
  const out = [];
  const grid = D.summarizeGrid(g.diff || D.diffGrid(g.saved.rows, g.rows));
  if (grid) out.push(grid);
  if (g.description.join('\n') !== g.saved.description.join('\n')) out.push('its description');
  return out.length ? out.join(', ') : '';
}
export const canRevert = () => !!(cur() && gauntletDirty(cur()));
export const canUndo = () => !!(cur() && H.canUndo(cur().hist));
export const canRedo = () => !!(cur() && H.canRedo(cur().hist));
export const undoLabel = () => { const e = cur() && H.nextUndo(cur().hist); return e ? e.label : ''; };
export const redoLabel = () => { const e = cur() && H.nextRedo(cur().hist); return e ? e.label : ''; };
export const current = () => cur();

// ---- checks: the solver after every edit; the real game's bot a moment after the edits stop
const timers = { check: new Map(), game: new Map() };
export function scheduleCheck(g, delay = 110) {
  clearTimeout(timers.check.get(g.name));
  g.checkPending = true;
  timers.check.set(g.name, setTimeout(() => runCheck(g), delay));
}
function request(g, game) {
  const lv = levelOf(g);
  return { name: g.name, rows: g.rows, game, ...(lv ? { level: lv.number, levelText: lv.text } : {}) };
}
async function runCheck(g) {
  const k = checkKeyOf(g);
  let res;
  try { res = await api('POST', '/api/check', request(g, false)); } catch (e) { res = { ok: false, error: e.message }; }
  g.checkPending = false;
  if (checkKeyOf(g) !== k) return; // (edited since: a newer check is coming)
  g.check = res; g.checkKey = k;
  if (g === cur() && S.active) { clampReplay(); renderPanels(); renderTrack(); renderReadout(); redraw(); }
  renderTabs();
  emit('gauntlet-checked', g);
}
export function scheduleGame(g, delay = 1400) {
  clearTimeout(timers.game.get(g.name));
  if (!levelOf(g)) { g.gameBusy = false; g.game = null; g.gameKey = checkKeyOf(g); return; } // (in no level: no run)
  g.gameBusy = true;
  timers.game.set(g.name, setTimeout(() => runGame(g), delay));
}
async function runGame(g) {
  const k = checkKeyOf(g);
  let res;
  try { res = await api('POST', '/api/check', request(g, true)); } catch (e) { res = { ok: false, error: e.message }; }
  if (res.cancelled || checkKeyOf(g) !== k) return;
  g.game = res.ok ? res.game : { error: res.error }; g.gameKey = k; g.gameBusy = false;
  if (g === cur() && S.active) { renderPanels(); redraw(); }
  renderTabs();
  emit('gauntlet-checked', g);
}
// what the level view needs to know of it: checked (and played) as it is now
export function ensureFresh(g) {
  if (!fresh(g) && !g.checkPending) scheduleCheck(g, 50);
  if (levelOf(g) && !gameFresh(g) && !g.gameBusy) scheduleGame(g, 300);
}
// a level's text changed: the gauntlet it lays is judged where it is now
export function levelChanged(name) {
  const g = store.gauntlets.find((x) => x.name === name);
  if (!g || fresh(g)) return;
  scheduleCheck(g, 400);
  scheduleGame(g, 2200);
}

// What the tests want of a gauntlet (Gauntlet_test, GameGauntlet_test, Campaign_test), item by item
export function verdict(g) {
  if (!g.check || !fresh(g)) return { state: 'busy', items: [] };
  const ch = g.check, lv = levelOf(g);
  if (!ch.ok) return { state: 'bad', items: [{ ok: false, what: 'A valid grid', why: ch.error || 'it doesn’t parse' }] };
  const s = ch.solution, hard = ch.meter[1], items = [], need = needOf(ch), slow = rateOf(g, 1);
  items.push({ ok: true, what: 'A valid grid', why: `${ch.cols} × ${ch.rows}, closed, a door each side` });
  items.push({ ok: s.reachable, what: 'Out, by stacking alone', why: s.reachable ? `${s.minEggs} eggs at the least` : 'no way to the exit: turn on “Where she gets” to see where she’s stuck' });
  if (s.reachable) {
    items.push({ ok: s.routes === 1, what: 'One way through', why: s.routes === 1 ? 'every cell on it unavoidable' : `${s.routes - 1} cells on other ways through` });
    items.push({ ok: s.softLocks === 0, what: 'No traps', why: s.softLocks ? `${s.softLocks} places she’d bounce forever (a dead end has to end her run)` : s.deadEndCells ? `${s.deadEndCells} dead-end cells, each one ending her run` : 'no dead ends at all' });
    items.push({ ok: hard.out, what: 'Out with the meter at its hardest', why: hard.out ? `refilling ${slow}× slower, 2 eggs at the door (${hard.waiting} waits)${ch.hard ? `, as a “hard ${ch.hard}” maze is judged` : ''}` : `out of eggs at col ${hard.crash[0]}, row ${hard.crash[1]}`, at: hard.out ? null : { c: hard.crash[0], b: hard.crash[1] + hard.crash[2] } });
    items.push({ ok: s.nearMisses >= need, what: `${need}+ near misses`, why: `${s.nearMisses} on the bot’s way` });
  }
  items.push({ ok: !ch.sameAs.length, what: 'Unlike every other gauntlet', why: ch.sameAs.length ? `the same as ${ch.sameAs.join(', ')} (mirror images count)` : 'mirror images included' });
  for (const d of ch.designs || []) items.push({ ok: d.ok, what: d.what, why: d.why });
  if (!lv) {
    items.push({ note: true, what: 'In no level yet', why: 'a level plays it once its course lays it: Levels → the level → Gauntlet' });
    const bad = items.some((it) => it.ok === false);
    return { state: bad ? 'bad' : 'ok', items };
  }
  const co = ch.course, fits = co.placed && co.x1 < co.finish;
  items.push({ ok: fits, what: `On level ${lv.number}’s course`, why: co.placed ? `${Math.round(co.x0)}–${Math.round(co.x1)} m of ${Math.round(co.finish)} m` : 'its course doesn’t lay it', level: lv.number });
  const gm = gameFresh(g) ? g.game : null;
  if (!gm) items.push({ busy: true, what: 'The game’s bot gets out', why: 'playing it in the real game…' });
  else if (gm.error) items.push({ ok: false, what: 'The game’s bot gets out', why: gm.error });
  else {
    items.push({ ok: gm.out, what: 'The game’s bot gets out', why: gm.out ? `${gm.inside.toFixed(1)} s inside` : gm.crash ? `it crashed at col ${Math.round(gm.crash[0])}, row ${Math.round(gm.crash[1])}` : 'it never got in',
      at: gm.crash ? { c: Math.round(gm.crash[0]), b: Math.round(gm.crash[1]) } : null });
    if (gm.out) {
      const shown = gm.perfects + gm.closeCalls;
      items.push({ ok: shown >= need && Math.abs(shown - s.nearMisses) <= 2, what: 'The game shows its near misses', why: `${shown} shown, the solver’s ${s.nearMisses} (within 2)` });
    }
  }
  const bad = items.some((it) => it.ok === false), busy = items.some((it) => it.busy);
  return { state: bad ? 'bad' : busy ? 'busy' : 'ok', items };
}

// ---- the top bar's gauntlet tabs
export function renderTabs() {
  const tabs = $('tabs');
  tabs.innerHTML = '';
  const byLevel = [...store.gauntlets].sort((a, b) => (levelNo(a) || 999) - (levelNo(b) || 999) || a.name.localeCompare(b.name));
  for (const g of byLevel) {
    const v = verdict(g), n = levelNo(g);
    const b = el('button', 'tab', `<span class="state ${v.state}"></span><span class="name">${esc(titled(g.name))}</span><span class="lvl${n ? '' : ' none'}">${n ? `LV ${n}` : 'NO LEVEL'}</span>${gauntletDirty(g) ? '<span class="unsaved"></span>' : ''}`);
    b.setAttribute('aria-selected', String(g === cur()));
    b.dataset.tipTitle = `The ${titled(g.name)}${n ? ` · level ${n}` : ' · in no level yet'}`;
    b.dataset.tip = `${v.state === 'ok' ? 'Ready to ship.' : v.state === 'bad' ? 'Not ready: its checks fail.' : 'Checking…'}${gauntletDirty(g) ? ` Unsaved: ${changeSummary(g) || 'edited'}.` : ''}`;
    b.onclick = () => emit('show-gauntlet', g.name);
    tabs.append(b);
  }
  const add = el('button', 'tab add', '<svg viewBox="0 0 24 24"><path d="M12 5v14M5 12h14"/></svg><span>New gauntlet</span>');
  setTip(add, 'prop.new', 'In no level till a level lays it (Levels → the level → Gauntlet).');
  add.onclick = () => newGauntlet();
  tabs.append(add);
  const on = tabs.querySelector('[aria-selected="true"]');
  if (on) tabs.scrollLeft = Math.max(0, Math.min(tabs.scrollLeft, on.offsetLeft - 8), on.offsetLeft + on.offsetWidth - tabs.clientWidth + 8);
}

// A new gauntlet, from a plain room (G.template): named here, saved with Save (until then, a draft in this browser)
export async function newGauntlet(suggest = '') {
  const taken = new Set(store.gauntlets.map((g) => g.name));
  const name = await askText({ title: 'A new gauntlet', ok: 'Make it', value: suggest, placeholder: 'e.g. torii',
    body: 'Its name: lower case letters, digits and _ (it names its file, assets/gauntlets/<name>.txt). It starts as a plain room to design from, in no level till one lays it.',
    check: (v) => (!v ? 'A name, to make it' : !/^[a-z][a-z0-9_]{0,31}$/.test(v) ? 'A letter first, then lower case letters, digits or _ (up to 32)' : taken.has(v) ? `There’s a gauntlet called ${v} already` : null) });
  if (name === null) return null;
  const g = gauntlet({ name, rows: G.template(), description: [`The ${name}: (describe its idea: what it teaches, tests, then twists).`], saved: null });
  store.gauntlets.push(g);
  refresh(g);
  emit('show-gauntlet', name);
  toast(`The ${titled(name)}: a plain room to design from. Save keeps it; a level’s Gauntlet setting puts it in a level.`, 'ok');
  return g;
}

// A gauntlet renamed: a saved one's file moved by the server (and the saved levels laying it rewritten); here, its
// draft and history under the new name, and every level laying it (levels.js gauntletRenamed). Nothing else changes
export async function renameGauntlet(g) {
  const taken = new Set(store.gauntlets.map((x) => x.name));
  const name = await askText({ title: `Rename the ${titled(g.name)}`, ok: 'Rename', value: g.name, placeholder: 'e.g. hall_of_saws',
    body: 'Lower case letters, digits and _ (its file, assets/gauntlets/<name>.txt; the game’s banner shows _ as spaces). The levels that lay it are rewritten to the new name; nothing else in them changes.',
    check: (v) => (!v ? 'A name, to rename it' : !/^[a-z][a-z0-9_]{0,31}$/.test(v) ? 'A letter first, then lower case letters, digits or _ (up to 32)' : v !== g.name && taken.has(v) ? `There’s a gauntlet called ${v} already` : null) });
  if (name === null || name === g.name) return false;
  const old = g.name;
  if (g.saved) {
    try { await api('POST', '/api/gauntlet/rename', { from: old, to: name }); } catch (e) { toast(e.message, 'bad'); return false; }
  }
  local.set(draftKey(g), null);
  H.keep('gauntlet:' + old, null);
  g.name = name;
  storeDraft(g); keepHistory(g);
  gauntletRenamed(old, name);
  emit('show-gauntlet', name); // (its address: #gauntlet/<name>)
  renderTabs(); renderPanels(); emit('top');
  toast(`The ${titled(old)} is the ${titled(name)} now${levelOf(g) ? `, in level ${levelNo(g)} too` : ''}`, 'ok');
  return true;
}

// ---- the tools column
function swatch(ch) {
  const cv = el('canvas');
  cv.width = cv.height = 56;
  const ctx = cv.getContext('2d');
  ctx.scale(2, 2);
  ctx.fillStyle = C.air; ctx.fillRect(0, 0, 28, 28);
  const fake = { ctx, z: 28 };
  if (ch === '^') { // a spring: a stone floor with the pad on it
    ctx.fillStyle = C.stone; ctx.fillRect(0, 21, 28, 7); ctx.fillStyle = C.stoneLit; ctx.fillRect(2, 23, 24, 4);
    ctx.strokeStyle = C.stoneEdge; ctx.lineWidth = 1.5; ctx.beginPath(); ctx.moveTo(0, 21.5); ctx.lineTo(28, 21.5); ctx.stroke();
    view.spring.call({ ctx, z: 24 }, 14, 21, 0.4, false);
    return cv;
  }
  view.cell.call(fake, ch, 0, 0, 28);
  if ('#HSC()'.includes(ch)) { ctx.strokeStyle = 'HC'.includes(ch) ? C.cubeEdge : C.stoneEdge; ctx.lineWidth = 2; ctx.strokeRect(1, 1, 26, 26); }
  if (ch === 'S' || ch === 'C') { ctx.save(); ctx.beginPath(); ctx.rect(0, 0, 28, 28); ctx.clip(); view.blade.call(fake, 14, 8, 22); ctx.restore(); }
  return cv;
}
function renderTools() {
  const t = $('tools');
  t.innerHTML = '';
  t.append(el('h4', null, 'Cells'));
  const sw = el('div', 'swatches');
  t.append(sw);
  for (const cell of G.CELLS) {
    const b = setTip(el('button', 'swatch'), CELL_TIP[cell.ch], 'Click: the first cell; right click: the second.');
    b.setAttribute('aria-label', cell.name);
    b.append(swatch(cell.ch), el('span', 'k', cell.key));
    if (cell.ch === S.primary) { b.classList.add('primary'); b.append(el('span', 'tag', '1ST')); }
    else if (cell.ch === S.secondary) { b.classList.add('secondary'); b.append(el('span', 'tag', '2ND')); }
    b.onclick = () => setPrimary(cell.ch);
    b.oncontextmenu = (e) => { e.preventDefault(); setSecondary(cell.ch); };
    sw.append(b);
  }
  t.append(el('div', 'rule'), el('h4', null, 'Tools'));
  for (const tool of TOOLS) {
    const b = setTip(el('button', 'tool' + (S.tool === tool.id ? ' on' : ''), `<svg viewBox="0 0 24 24">${tool.icon}</svg><span>${tool.label}</span>`), tool.tip);
    b.onclick = () => setTool(tool.id);
    t.append(b);
  }
}
function setPrimary(ch) { if (ch === S.secondary) S.secondary = S.primary; S.primary = ch; if (S.tool === 'pick' || S.tool === 'select') S.tool = 'brush'; renderTools(); }
function setSecondary(ch) { if (DOOR(ch)) return; if (ch === S.primary) S.primary = S.secondary; S.secondary = ch; renderTools(); }
function setTool(id) { S.tool = id; S.preview = null; if (id !== 'select') { S.selection = null; S.pasting = null; } renderTools(); redraw(); }
const DOOR = (ch) => G.DOORS.has(ch);

// ---- overlays
function renderChips() {
  const c = $('chips');
  c.innerHTML = '';
  for (const o of OVERLAYS) {
    const b = setTip(el('button', 'chip' + (S.overlays[o.id] ? ' on' : ''), `<span class="dot" style="background:${o.color}"></span>${o.label}`), o.tip);
    b.onclick = () => { S.overlays[o.id] = !S.overlays[o.id]; renderChips(); redraw(); };
    c.append(b);
  }
}

// ---- the inspector
export function renderPanels() {
  const g = cur();
  if (!g || !S.active) return;
  const v = verdict(g), lv = levelOf(g);
  const vd = $('verdict');
  const head = v.state === 'ok' ? ['ok', 'Ready to ship', lv ? 'Every check the tests make passes.' : 'Every check passes; it’s in no level yet.'] : v.state === 'bad' ? ['bad', 'Not ready', 'The tests will fail (and block commits) until these pass.'] : ['busy', 'Checking', 'The solver, then the real game…'];
  vd.innerHTML = `<h3>${esc(titled(g.name))} <button class="aside link rename">rename</button>${lv ? ` <button class="aside link" data-level="${lv.number}" data-tip="Opens level ${lv.number}’s course." data-tip-title="Level ${lv.number}">level ${lv.number} ↗</button>` : ' <span class="aside">in no level</span>'}</h3><div class="headline"><span class="badge ${head[0]}">${head[1]}</span><span class="sub">${head[2]}</span></div>`;
  const rename = vd.querySelector('.rename');
  setTip(rename, 'gauntlet.rename');
  rename.onclick = () => renameGauntlet(g);
  const back = vd.querySelector('[data-level]');
  if (back) back.onclick = () => emit('show-level', lv.number);
  const ul = el('ul', 'checks');
  for (const it of v.items) {
    const li = el('li', it.busy ? 'busy' : it.note ? 'note' : it.ok ? 'ok' : 'bad', `<span class="mark">${it.busy ? '…' : it.note ? 'i' : it.ok ? '✓' : '✕'}</span><span><div class="what">${esc(it.what)}</div><div class="why">${esc(it.why)}</div></span>`);
    if (it.at) { li.classList.add('link'); li.dataset.tip = 'Click: show me where, in the grid.'; li.dataset.tipSide = 'left'; li.onclick = () => lookAt(it.at.c, it.at.b); }
    else if (it.level) { li.classList.add('link'); li.dataset.tip = `Click: open level ${it.level}’s course.`; li.dataset.tipSide = 'left'; li.onclick = () => emit('show-level', it.level); }
    ul.append(li);
  }
  vd.append(ul);
  setTip(vd.querySelector('h3'), 'card.verdict');
  renderHistoryCard(g); renderNumbers(g); renderGame(g); renderMisses(g); renderNotes(g);
  const ta = $('description');
  if (document.activeElement !== ta) ta.value = g.description.join('\n');
}
function renderHistoryCard(g) {
  const box = $('ghistory');
  renderHistory(box, { t: g.hist, summary: changeSummary(g),
    onJump: (i) => { jumpTo(g, i); emit('top'); },
    onHover: (f) => { S.focus = f; focusPulse(); },
    onShow: (f) => { if (f) { view.centerOn((f.c0 + f.c1) / 2, (f.i0 + f.i1) / 2); redraw(); } } });
  setTip(box.querySelector('h3'), 'card.history');
}
let pulsing = false;
function focusPulse() { // (a History entry hovered: its spot pulses on the grid while it is)
  if (pulsing) return;
  pulsing = true;
  const step = () => { if (!S.focus || !S.active) { pulsing = false; redraw(); return; } draw(); requestAnimationFrame(step); };
  requestAnimationFrame(step);
}
setInterval(() => { if (S.active && cur()) renderHistoryCard(cur()); }, 30000); // (how long ago, kept fresh)
function renderNumbers(g) {
  const n = $('numbers');
  const ch = g.check;
  if (!ch || !ch.ok) { n.innerHTML = '<h3>The run</h3><p class="hint">Numbers come with a valid grid.</p>'; return; }
  const r = run(g), s = ch.solution;
  const split = [0, 0, 0];
  for (const m of r.nearMisses) split[m[2]]++;
  const total = r.out ? r.nearMisses.length : s.nearMisses;
  const lv = levelOf(g);
  n.innerHTML = `<h3>The run <span class="aside">${fresh(g) ? '' : 'updating…'}</span></h3>
    <div class="stats">
      <div class="stat"><div class="v">${r.seconds.toFixed(1)}<small>s</small></div><div class="l">Inside, at ${rateOf(g, S.rate)}×</div></div>
      <div class="stat"><div class="v">${total}</div><div class="l">Near misses</div>
        <div class="nm-split">${KIND.map((k, i) => `<span data-tip="${k.label}"><i style="background:${k.color}"></i>${split[i]}</span>`).join('')}</div></div>
      <div class="stat"><div class="v">${s.reachable ? s.minEggs : '—'}</div><div class="l">Eggs at the least</div></div>
      <div class="stat"><div class="v">${ch.cols}<small>× ${ch.rows}</small></div><div class="l">Cells</div></div>
    </div>
    <table class="rates"><tr><th>Meter</th><th>Bot</th><th>Moves</th><th>Waits</th><th>Misses</th></tr>
    ${ch.meter.map((m, i) => `<tr class="${i === S.rate ? 'sel' : ''}"><td>${rateOf(g, i)}×</td><td class="${m.out ? 'out' : 'crash'}">${m.out ? 'out' : 'crash'}</td><td>${m.cells}</td><td>${m.waiting}</td><td>${m.out ? m.nearMisses.length : '—'}</td></tr>`).join('')}
    </table>
    <dl class="kv"><dt>Door in, out</dt><dd>row ${ch.entryRow}, row ${ch.exitRow}</dd><dt>An egg back every</dt><dd>${ch.cellsPerEgg.toFixed(1)} cells (${(ch.cellsPerEgg * ch.secondsPerCell).toFixed(2)} s)</dd>
    ${lv && ch.course.placed ? `<dt>On level ${lv.number}’s course</dt><dd>${Math.round(ch.course.x0)}–${Math.round(ch.course.x1)} m of ${Math.round(ch.course.finish)}</dd>` : `<dt>Its meter</dt><dd>level ${ch.level}’s</dd>`}</dl>`;
}
function renderGame(g) {
  const c = $('gamecard');
  const gm = gameFresh(g) ? g.game : null;
  let line;
  if (fresh(g) && !g.check.ok) line = '<span class="big bad">No run</span><span class="game-sub">the grid isn’t valid</span>';
  else if (!levelOf(g)) line = '<span class="big muted">No run</span><span class="game-sub">it’s in no level yet: the bot plays it once a level lays it</span>';
  else if (!gm) line = '<span class="big busy">Playing…</span><span class="game-sub">the real game, headless, the bot playing</span>';
  else if (gm.error) line = `<span class="big bad">No run</span><span class="game-sub">${esc(gm.error)}</span>`;
  else if (gm.out) line = `<span class="big ok">Out</span><span class="game-sub">${gm.inside.toFixed(1)} s inside · ${gm.perfects} PERFECT, ${gm.closeCalls} CLOSE SHAVE / CALL</span>`;
  else line = `<span class="big bad">Crashed</span><span class="game-sub">${gm.crash ? `at col ${Math.round(gm.crash[0])}, row ${Math.round(gm.crash[1])}, ${gm.inside.toFixed(1)} s in` : 'it never got in'}</span>`;
  c.innerHTML = `<h3>In the game <span class="aside">the bot, headless</span></h3><div class="game-line">${line}</div>`;
}
function renderMisses(g) {
  const c = $('misses');
  const r = run(g);
  if (!r) { c.innerHTML = '<h3>Near misses</h3><p class="hint">They come with a way through.</p>'; return; }
  c.innerHTML = `<h3>Near misses <span class="aside">as the game shows them, at ${rateOf(g, S.rate)}×</span></h3>`;
  if (!r.out) { c.insertAdjacentHTML('beforeend', '<p class="hint">The bot runs out of eggs at this meter: no run to count.</p>'); return; }
  const ul = el('ul', 'misslist');
  for (const [cc, b, kind, step] of r.nearMisses) {
    const li = el('li', R.step === step ? 'at' : '', `<i style="background:${KIND[kind].color}"></i><span>${KIND[kind].label}</span><span>col ${cc}, row ${b} · ${(step * g.check.secondsPerCell).toFixed(1)} s</span>`);
    li.onclick = () => { setStep(step); lookAt(cc, b); };
    ul.append(li);
  }
  c.append(ul);
}
function renderNotes(g) {
  const c = $('notes');
  c.hidden = !g.lints.length;
  if (!g.lints.length) return;
  c.innerHTML = `<h3>Notes <span class="aside">${g.lints.length}</span></h3>`;
  const ul = el('ul', 'notes');
  for (const l of g.lints.slice(0, 30)) {
    const li = el('li', null, `${esc(l.text)} <span style="color:var(--faint)">(col ${l.c}, row ${G.gameRow(g.rows, l.i)})</span>`);
    li.onclick = () => { view.centerOn(l.c, l.i); redraw(); };
    ul.append(li);
  }
  c.append(ul);
}
function lookAt(c, b) {
  const g = cur();
  view.centerOn(c, G.index(g.rows, b));
  redraw();
}

// ---- the replay: the bot's run at the chosen meter, move by move
function renderTransport() {
  const sp = $('rpSpeed');
  sp.innerHTML = '';
  for (const s of SPEEDS) {
    const b = el('button', R.speed === s ? 'on' : '', `${s}×`);
    b.onclick = () => { R.speed = s; renderTransport(); };
    sp.append(b);
  }
  const rt = $('rpRate');
  rt.innerHTML = '';
  RATES.forEach((r0, i) => {
    const r = rateOf(cur(), i);
    const b = el('button', S.rate === i ? 'on' : '', `${r}×`);
    b.dataset.tip = i ? `The meter refilling ${r} times slower than the level’s.` : 'The meter at the level’s own refill.';
    b.onclick = () => { S.rate = i; clampReplay(); renderTransport(); renderPanels(); renderTrack(); renderReadout(); redraw(); };
    rt.append(b);
  });
  $('rpPlay').classList.toggle('playing', R.playing);
}
function clampReplay() { const r = run(); R.step = r ? clamp(R.step, 0, r.steps.length - 1) : 0; }
function setStep(k, follow = true) {
  const r = run();
  if (!r) return;
  R.step = clamp(Math.round(k), 0, r.steps.length - 1);
  if (follow) keepInView(r.steps[R.step]);
  renderReadout(); renderTrack(); redraw();
  for (const li of $('misses').querySelectorAll('.misslist li')) li.classList.remove('at');
}
function keepInView(st) {
  const g = cur();
  const [c, b, n] = st;
  const x = view.ox + (c + 0.5) * view.z, y = view.oy + (g.rows.length - 1 - (b + n) + 0.5) * view.z;
  const mx = view.w * 0.2, my = view.h * 0.2;
  if (x < 30 + mx || x > view.w - mx || y < 20 + my || y > view.h - my) view.centerOn(c, g.rows.length - 1 - (b + n));
}
function playPause() {
  const r = run();
  if (!r) return;
  if (R.playing) { R.playing = false; renderTransport(); return; }
  if (R.step >= r.steps.length - 1) R.step = 0;
  R.playing = true; R.acc = 0; R.last = performance.now();
  renderTransport();
  requestAnimationFrame(tick);
}
function tick(t) {
  if (!R.playing) return;
  const g = cur(), r = run();
  if (!r || !fresh(g) || !S.active) { R.playing = false; renderTransport(); return; }
  R.acc += ((t - R.last) / 1000) * R.speed / g.check.secondsPerCell;
  R.last = t;
  const adv = Math.floor(R.acc);
  if (adv) { R.acc -= adv; setStep(R.step + adv); }
  if (R.step >= r.steps.length - 1) { R.playing = false; renderTransport(); return; }
  requestAnimationFrame(tick);
}
function renderReadout() {
  const g = cur(), r = run(g), o = $('readout');
  if (!g || !r || !r.steps.length) { o.innerHTML = '<div class="t">—</div>'; return; }
  const st = r.steps[R.step];
  const [c, b, n, , lay, move, climbed, fell, flags, meter] = st;
  const eggs = meter / 100;
  const icons = Array.from({ length: 8 }, (_, i) => {
    const f = clamp(eggs - i, 0, 1);
    return f >= 1 ? '<i class="full"></i>' : f > 0 ? `<i class="part" style="--f:${Math.round(f * 100)}%"></i>` : '<i></i>';
  }).join('');
  const ev = [];
  for (const m of r.nearMisses) if (m[3] === R.step) ev.push(`<span style="color:${KIND[m[2]].color}">${KIND[m[2]].label}</span>`);
  if (!ev.length) {
    if (move === 2) ev.push('<span style="color:var(--red)">CRASH: out of eggs, or into a wall</span>');
    else if (move === 1) ev.push('<span style="color:var(--muted)">bounce</span>');
    else if (fell) ev.push(`<span style="color:var(--muted)">drops ${fell}</span>`);
    else if (climbed) ev.push(`<span style="color:var(--muted)">climbs ${climbed}</span>`);
    else if (flags & 1) ev.push('<span style="color:var(--muted)">a blade takes an egg</span>');
  }
  if (lay) ev.push('<span style="color:var(--cyan)">+ an egg</span>');
  o.innerHTML = `<div class="t">${(R.step * g.check.secondsPerCell).toFixed(2)}s <small>move ${R.step + 1} of ${r.steps.length}</small></div>
    <div><div class="l">Where</div>col ${c}, row ${b}</div><div><div class="l">Stack</div>${n} egg${n === 1 ? '' : 's'}</div>
    <div style="grid-column:1/-1"><div class="l">Meter</div><div class="eggs">${icons}</div></div>
    <div class="ev">${ev.join(' · ')}</div>`;
}
function renderTrack() {
  const cv = $('track'), ctx = cv.getContext('2d');
  const rect = cv.getBoundingClientRect(), dpr = window.devicePixelRatio || 1;
  if (!rect.width) return;
  if (cv.width !== Math.round(rect.width * dpr) || cv.height !== Math.round(rect.height * dpr)) { cv.width = Math.round(rect.width * dpr); cv.height = Math.round(rect.height * dpr); }
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  const W = rect.width, H = rect.height;
  ctx.fillStyle = '#0a0612'; ctx.fillRect(0, 0, W, H);
  const g = cur(), r = run(g);
  if (!g || !r || !r.steps.length) { ctx.fillStyle = 'rgba(236,230,255,0.4)'; ctx.font = '500 12px "Chakra Petch"'; ctx.fillText(g && g.check && !g.check.ok ? 'No run: the grid isn’t valid' : 'Waiting for the solver…', 12, H / 2); return; }
  const N = r.steps.length, top = 16, bot = H - 14, x = (k) => 8 + (k / Math.max(1, N - 1)) * (W - 16);
  const maxN = Math.max(8, ...r.steps.map((s) => s[2]));
  ctx.font = '500 10px "Chakra Petch"'; ctx.textBaseline = 'alphabetic';
  const sec = g.check.secondsPerCell, every = N * sec > 30 ? 5 : 2;
  for (let t = 0; t <= N * sec; t += every) {
    const xx = x(t / sec);
    ctx.fillStyle = 'rgba(255,255,255,0.06)'; ctx.fillRect(xx, top, 1, bot - top);
    ctx.fillStyle = 'rgba(236,230,255,0.35)'; ctx.fillText(`${t}s`, xx + 3, H - 3);
  }
  const yM = (v) => bot - (v / 8) * (bot - top), yN = (v) => bot - (v / maxN) * (bot - top);
  ctx.beginPath(); ctx.moveTo(x(0), bot);
  r.steps.forEach((s, k) => ctx.lineTo(x(k), yM(s[9] / 100)));
  ctx.lineTo(x(N - 1), bot); ctx.closePath();
  ctx.fillStyle = 'rgba(41,231,255,0.14)'; ctx.fill();
  ctx.strokeStyle = 'rgba(41,231,255,0.8)'; ctx.lineWidth = 1.2; ctx.beginPath();
  r.steps.forEach((s, k) => (k ? ctx.lineTo(x(k), yM(s[9] / 100)) : ctx.moveTo(x(k), yM(s[9] / 100)))); ctx.stroke();
  ctx.strokeStyle = 'rgba(244,255,90,0.75)'; ctx.beginPath();
  r.steps.forEach((s, k) => (k ? ctx.lineTo(x(k), yN(s[2])) : ctx.moveTo(x(k), yN(s[2])))); ctx.stroke();
  for (const [, , kind, step] of r.nearMisses) { ctx.fillStyle = KIND[kind].color; ctx.beginPath(); ctx.arc(x(step), 8, 3, 0, Math.PI * 2); ctx.fill(); }
  if (!r.out) { ctx.fillStyle = C.crash; ctx.fillRect(x(N - 1) - 1, top - 6, 3, bot - top + 6); }
  const px = x(R.step);
  ctx.fillStyle = '#fff'; ctx.fillRect(px - 1, 2, 2, H - 4);
  ctx.beginPath(); ctx.moveTo(px - 5, 2); ctx.lineTo(px + 5, 2); ctx.lineTo(px, 8); ctx.fill();
  ctx.fillStyle = 'rgba(236,230,255,0.5)'; ctx.textAlign = 'right'; ctx.fillText('meter', W - 8, top + 8); ctx.fillStyle = 'rgba(244,255,90,0.7)'; ctx.fillText('stack', W - 8, top + 20); ctx.textAlign = 'left';
}
function trackStep(e) {
  const r = run();
  if (!r) return;
  const rect = $('track').getBoundingClientRect();
  setStep(((e.clientX - rect.left - 8) / (rect.width - 16)) * (r.steps.length - 1));
}

// ---- drawing the board
let frame = 0;
export function redraw() { if (!frame && S.active) frame = requestAnimationFrame(draw); }
function draw() {
  frame = 0;
  const g = cur();
  if (!g || !S.active) return;
  const fr = fresh(g);
  const rows = S.drag && S.drag.kind === 'move' ? S.drag.base : g.rows;
  const sameSize = g.check && g.check.ok && g.check.rows === g.rows.length && g.check.cols === G.cols(g.rows);
  const selection = S.selection && S.drag && S.drag.kind === 'move' ? { ...S.selection, float: S.drag.float } : S.pasting ? { ...S.pasting.rect, float: S.pasting } : S.selection;
  const st = mazeStart(g);
  view.draw({ rows, check: g.check, fresh: fr, stale: !fr && sameSize, overlays: S.overlays, changed: g.changed, hover: S.hover, preview: S.preview,
    selection, run: fr || sameSize ? run(g) : null, replay: { step: R.step }, lints: g.lints, game: gameFresh(g) ? g.game : null,
    start: st, hoverStart: st && S.hover && S.hover.c === st.c && (G.gameRow(rows, S.hover.i) === st.b || G.gameRow(rows, S.hover.i) === st.b + 1), focus: S.focus });
  view.minimap(rows, fr || sameSize ? run(g) : null, true);
  if (view.animated && !frame) frame = requestAnimationFrame(draw); // (a spring's plate bobs, a flight's dots march: keep drawing while one is on screen)
}

// ---- pointing at the board
const pointOf = (e) => { const r = board.getBoundingClientRect(); return { x: e.clientX - r.left, y: e.clientY - r.top }; };
function lineCells(c0, i0, c1, i1) { // (a stroke's cells between two pointer samples)
  const out = [], dc = Math.abs(c1 - c0), di = Math.abs(i1 - i0), sc = c0 < c1 ? 1 : -1, si = i0 < i1 ? 1 : -1;
  let err = dc - di, c = c0, i = i0;
  for (;;) { out.push([c, i]); if (c === c1 && i === i1) break; const e2 = 2 * err; if (e2 > -di) { err -= di; c += sc; } if (e2 < dc) { err += dc; i += si; } }
  return out;
}
const inSel = (c, i) => { const s = S.selection; return s && c >= Math.min(s.c0, s.c1) && c <= Math.max(s.c0, s.c1) && i >= Math.min(s.i0, s.i1) && i <= Math.max(s.i0, s.i1); };
function pick(c, i) { const g = cur(); if (view.inGrid(c, i)) setPrimary(g.rows[i][c]); }

board.addEventListener('pointerdown', (e) => {
  const g = cur();
  if (!g) return;
  const p = pointOf(e);
  board.setPointerCapture(e.pointerId);
  if (e.button === 1 || S.space) { S.drag = { kind: 'pan', x: p.x, y: p.y }; boardWrap.classList.add('panning'); return; }
  const { c, i } = view.cellAt(p.x, p.y);
  const right = e.button === 2;
  clearTimeout(cellTimer); S.cellTip = null; hideTip();
  if (S.pasting) { commit(G.paste(g.rows, S.pasting.block, S.pasting.c, S.pasting.i), 'paste'); S.selection = { ...S.pasting.rect }; S.pasting = null; renderTools(); return; }
  switch (S.tool) {
    case 'start': return placeStart(g, c, i, right || e.altKey);
    case 'brush': {
      if (e.altKey) return pick(c, i);
      const ch = right ? S.secondary : S.primary;
      if (DOOR(ch)) { commit(G.moveDoor(g.rows, ch, i), 'door'); return; }
      S.drag = { kind: 'brush', snapshot: g.rows, ch, last: { c, i } };
      g.rows = G.setCell(g.rows, c, i, ch);
      return changedLive(g);
    }
    case 'rect': {
      const ch = right ? S.secondary : S.primary;
      if (DOOR(ch)) return;
      S.drag = { kind: 'rect', ch }; S.preview = { kind: 'rect', c0: c, i0: i, c1: c, i1: i, ch };
      return redraw();
    }
    case 'select': {
      if (inSel(c, i)) { // lift it: what it leaves filled from its sides
        const s = S.selection, ca = Math.min(s.c0, s.c1), ia = Math.min(s.i0, s.i1);
        const block = G.extract(g.rows, s.c0, s.i0, s.c1, s.i1);
        S.drag = { kind: 'move', snapshot: g.rows, base: G.vacate(g.rows, s.c0, s.i0, s.c1, s.i1, S.secondary), from: { c, i }, float: { block, c: ca, i: ia } };
        return redraw();
      }
      S.drag = { kind: 'select' }; S.selection = { c0: c, i0: i, c1: c, i1: i };
      return redraw();
    }
    case 'cols': case 'rows': {
      const remove = right || e.altKey;
      S.drag = { kind: S.tool, remove };
      S.preview = S.tool === 'cols' ? { kind: 'cols', c0: c, c1: c, remove } : { kind: 'rows', i0: i, i1: i, remove };
      return redraw();
    }
    case 'pick': return pick(c, i);
    default: break;
  }
});
board.addEventListener('pointermove', (e) => {
  const g = cur();
  if (!g) return;
  const p = pointOf(e);
  const d = S.drag;
  if (d && d.kind === 'pan') { view.panBy(p.x - d.x, p.y - d.y); d.x = p.x; d.y = p.y; return redraw(); }
  const { c, i } = view.cellAt(p.x, p.y);
  const moved = !S.hover || S.hover.c !== c || S.hover.i !== i;
  S.hover = { c, i };
  if (moved) renderHover(e);
  cellCard(e);
  if (S.pasting) { S.pasting.c = c; S.pasting.i = i; S.pasting.rect = { c0: c, i0: i, c1: c + S.pasting.block[0].length - 1, i1: i + S.pasting.block.length - 1 }; }
  if (!d) {
    if (S.tool === 'cols' && moved) S.preview = view.inGrid(c, i) ? { kind: 'cols', c0: c, c1: c, remove: e.altKey } : null;
    if (S.tool === 'rows' && moved) S.preview = view.inGrid(c, i) ? { kind: 'rows', i0: i, i1: i, remove: e.altKey } : null;
    return redraw();
  }
  if (!moved) return;
  if (d.kind === 'brush') {
    let rows = g.rows;
    for (const [cc, ii] of lineCells(d.last.c, d.last.i, c, i)) rows = G.setCell(rows, cc, ii, d.ch);
    d.last = { c, i };
    if (rows !== g.rows) { g.rows = rows; changedLive(g); }
  } else if (d.kind === 'rect') { S.preview.c1 = c; S.preview.i1 = i; }
  else if (d.kind === 'select') { S.selection.c1 = c; S.selection.i1 = i; }
  else if (d.kind === 'move') {
    const s = S.selection;
    d.float.c = Math.min(s.c0, s.c1) + (c - d.from.c); d.float.i = Math.min(s.i0, s.i1) + (i - d.from.i);
  } else if (d.kind === 'cols') S.preview.c1 = c;
  else if (d.kind === 'rows') S.preview.i1 = i;
  redraw();
});
function endDrag() {
  const g = cur(), d = S.drag;
  S.drag = null;
  boardWrap.classList.remove('panning');
  if (!g || !d) return;
  if (d.kind === 'brush') { if (!G.same(g.rows, d.snapshot)) { const rows = g.rows; g.rows = d.snapshot; commit(rows, 'brush', { snapshot: d.snapshot }); } }
  else if (d.kind === 'rect') { const p = S.preview; S.preview = null; commit(G.fillRect(g.rows, p.c0, p.i0, p.c1, p.i1, d.ch), 'rect'); }
  else if (d.kind === 'move') {
    const s = S.selection, w = Math.abs(s.c1 - s.c0), h = Math.abs(s.i1 - s.i0);
    if (d.float.c === Math.min(s.c0, s.c1) && d.float.i === Math.min(s.i0, s.i1)) return redraw(); // (not moved)
    commit(G.paste(d.base, d.float.block, d.float.c, d.float.i), 'move', { snapshot: d.snapshot });
    S.selection = { c0: d.float.c, i0: d.float.i, c1: d.float.c + w, i1: d.float.i + h };
  } else if (d.kind === 'cols' || d.kind === 'rows') {
    const p = S.preview; S.preview = null; S.selection = null;
    const rows = d.kind === 'cols' ? (d.remove ? G.deleteCols(g.rows, p.c0, p.c1) : G.duplicateCols(g.rows, p.c0, p.c1))
      : (d.remove ? G.deleteRows(g.rows, p.i0, p.i1) : G.duplicateRows(g.rows, p.i0, p.i1));
    if (rows === g.rows && d.kind === 'rows' && d.remove) toast('A door’s row stays (move the door first), and a grid keeps 4 rows at least', 'bad');
    const n = G.cols(g.rows), a = d.kind === 'cols' ? Math.max(1, Math.min(p.c0, p.c1)) : Math.min(p.i0, p.i1), b = d.kind === 'cols' ? Math.min(n - 2, Math.max(p.c0, p.c1)) : Math.max(p.i0, p.i1);
    const hint = d.kind === 'cols' ? (d.remove ? { c0: a, c1: b } : { c0: b + 1, c1: b + 1 + (b - a) }) : { i0: a, i1: b };
    commit(rows, `${d.kind}-${d.remove ? 'out' : 'in'}`, { hint });
  }
  redraw();
}
board.addEventListener('pointerup', endDrag);
board.addEventListener('pointercancel', endDrag);
board.addEventListener('pointerleave', () => { if (!S.drag) { S.hover = null; if (S.tool === 'cols' || S.tool === 'rows') S.preview = null; renderHover(); redraw(); } clearTimeout(cellTimer); S.cellTip = null; hideTip(); });
board.addEventListener('contextmenu', (e) => e.preventDefault());
board.addEventListener('wheel', (e) => {
  e.preventDefault();
  const p = pointOf(e);
  if (e.ctrlKey || e.metaKey) view.zoomAt(p.x, p.y, Math.exp(-e.deltaY * (e.ctrlKey && !e.metaKey ? 0.01 : 0.0025)));
  else if (e.shiftKey && !e.deltaX) view.panBy(-e.deltaY, 0);
  else view.panBy(-e.deltaX, -e.deltaY);
  redraw();
}, { passive: false });
function changedLive(g) { refresh(g, { live: true }); } // (mid-stroke: drawn and checked, the undo step comes at its end)
// ---- the play-test's start in this maze (the level laying it keeps it: levels.js)
function mazeStart(g) { const lv = levelOf(g), st = lv && startOf(lv); return st && st.gauntlet === g.name ? st : null; }
function placeStart(g, c, i, clear) {
  const lv = levelOf(g);
  if (!lv) return toast(`The ${titled(g.name)} is in no level yet: Play needs a level to start it in`, 'bad');
  const st = mazeStart(g);
  if (clear) { if (st) { clearStart(lv); toast('Start point cleared: Play starts at its door'); } return redraw(); }
  if (st && st.c === c && (G.gameRow(g.rows, i) === st.b || G.gameRow(g.rows, i) === st.b + 1)) { setStart(lv, { ...st, dir: -st.dir }); return renderStartChip(); }
  const cell = G.standable(g.rows, c, i);
  if (!cell) return toast('She can’t start there: on a floor (not a blade), with two cells of air for her body', 'bad');
  setStart(lv, { gauntlet: g.name, c: cell.c, b: cell.b, dir: 1 });
  redraw();
}
function renderStartChip() { emit('top'); redraw(); }
let cellTimer = 0;
function cellCard(e) { // (what's under the pointer, a moment after it stops)
  const g = cur(), h = S.hover;
  clearTimeout(cellTimer);
  if (!g || !h || !view.inGrid(h.c, h.i) || S.drag) { if (S.cellTip) { S.cellTip = null; hideTip(); } return; }
  const ch = g.rows[h.i][h.c], key = CELL_TIP[ch], was = g.was && g.was.get(h.c + ',' + h.i), st = mazeStart(g);
  const onStart = st && st.c === h.c && (G.gameRow(g.rows, h.i) === st.b || G.gameRow(g.rows, h.i) === st.b + 1);
  const tip = onStart ? TIPS['hazard.start'] : TIPS[key];
  if (!tip) return;
  const extra = [`Col ${h.c}, row ${G.gameRow(g.rows, h.i)}.`, was && !onStart ? `Was ${D.cellName(was)} at the save.` : ''].filter(Boolean).join(' ');
  const id = `${h.c},${h.i},${onStart}`;
  const content = { title: tip.title, text: `${tip.text} ${extra}`.trim() };
  if (S.cellTip === id) return showAt(e.clientX, e.clientY, content);
  if (S.cellTip) { S.cellTip = null; hideTip(); }
  const x = e.clientX, y = e.clientY;
  cellTimer = setTimeout(() => { if (S.drag) return; S.cellTip = id; showAt(x, y, content); }, 520);
}
function renderHover() {
  const g = cur(), h = S.hover, out = $('hoverinfo');
  if (!g || !h || !view.inGrid(h.c, h.i)) { out.innerHTML = g ? `${G.cols(g.rows)} × ${g.rows.length} · the street is row 0` : ''; return; }
  const ch = g.rows[h.i][h.c], b = G.gameRow(g.rows, h.i);
  let extra = '';
  const r = run(g);
  if (r && fresh(g)) for (const m of r.nearMisses) if (m[0] === h.c && m[1] === b) extra += ` · <b style="color:${KIND[m[2]].color}">${KIND[m[2]].label}</b>`;
  const map = g.check && g.check.ok && fresh(g) ? g.check.cellMap[h.i][h.c] : '';
  if (map === 'w') extra += ' · on the way through'; else if (map === 'd') extra += ' · <b style="color:var(--red)">a dead end</b>';
  out.innerHTML = `col <b>${h.c}</b> · row <b>${b}</b> · ${esc(G.CELL[ch] ? G.CELL[ch].name : ch)}${extra}`;
}

// the minimap: click or drag to look around
const mini = $('minimap');
let miniDrag = false;
function miniLook(e) { const r = mini.getBoundingClientRect(), p = view.miniToCell(e.clientX - r.left, e.clientY - r.top); if (p) { view.centerOn(p.c, p.i); redraw(); } }
mini.addEventListener('pointerdown', (e) => { miniDrag = true; mini.setPointerCapture(e.pointerId); miniLook(e); });
mini.addEventListener('pointermove', (e) => { if (miniDrag) miniLook(e); });
mini.addEventListener('pointerup', () => { miniDrag = false; });

// the track: click or drag to look
const track = $('track');
let trackDrag = false;
track.addEventListener('pointerdown', (e) => { trackDrag = true; track.setPointerCapture(e.pointerId); R.playing = false; renderTransport(); trackStep(e); });
track.addEventListener('pointermove', (e) => { if (trackDrag) trackStep(e); });
track.addEventListener('pointerup', () => { trackDrag = false; });

// ---- selections: copy, cut, paste, flip, fill
function copySel() { const g = cur(), s = S.selection; if (g && s) { S.clipboard = G.extract(g.rows, s.c0, s.i0, s.c1, s.i1); toast('Copied: ⌘V to paste it'); } }
function cutSel() { const g = cur(), s = S.selection; if (g && s) { S.clipboard = G.extract(g.rows, s.c0, s.i0, s.c1, s.i1); commit(G.vacate(g.rows, s.c0, s.i0, s.c1, s.i1, S.secondary), 'cut'); S.selection = null; } }
function startPaste() {
  const g = cur();
  if (!g || !S.clipboard) return;
  const h = S.hover && view.inGrid(S.hover.c, S.hover.i) ? S.hover : { c: 1, i: 1 };
  S.tool = 'select'; renderTools();
  S.pasting = { block: S.clipboard, c: h.c, i: h.i, rect: { c0: h.c, i0: h.i, c1: h.c + S.clipboard[0].length - 1, i1: h.i + S.clipboard.length - 1 } };
  redraw();
}
function flipSel() { const g = cur(), s = S.selection; if (g && s) commit(G.paste(g.rows, G.flip(G.extract(g.rows, s.c0, s.i0, s.c1, s.i1)), Math.min(s.c0, s.c1), Math.min(s.i0, s.i1)), 'flip'); }
function fillSel() { const g = cur(), s = S.selection; if (g && s) commit(G.fillRect(g.rows, s.c0, s.i0, s.c1, s.i1, S.secondary), 'fill'); }

// ---- the keyboard (the editor's shell hands it what's this view's): true if it took the key
export function keydown(e) {
  const mod = e.metaKey || e.ctrlKey, k = e.key.toLowerCase();
  if (mod && k === 'c') { copySel(); return true; }
  if (mod && k === 'x') { e.preventDefault(); cutSel(); return true; }
  if (mod && k === 'v') { e.preventDefault(); startPaste(); return true; }
  if (mod) return false;
  if (e.key === ' ') { e.preventDefault(); if (!S.space) { S.space = true; boardWrap.classList.add('pan'); } return true; }
  const cell = G.CELLS.find((c) => c.key === e.key);
  if (cell) { setPrimary(cell.ch); return true; }
  const tool = TOOLS.find((t) => t.key === k);
  if (tool) { setTool(tool.id); return true; }
  switch (e.key) {
    case 'x': case 'X': if (!DOOR(S.primary)) { [S.primary, S.secondary] = [S.secondary, S.primary]; renderTools(); } return true;
    case 'f': case 'F': if (S.selection) flipSel(); else { view.fit(cur().rows); redraw(); } return true;
    case '-': view.zoomAt(view.w / 2, view.h / 2, 1 / 1.25); redraw(); return true; // ([ and ] are the laser cells here)
    case '+': view.zoomAt(view.w / 2, view.h / 2, 1.25); redraw(); return true;
    case 'p': case 'P': playPause(); return true;
    case ',': R.playing = false; renderTransport(); setStep(R.step - 1); return true;
    case '.': R.playing = false; renderTransport(); setStep(R.step + 1); return true;
    case 'Home': setStep(0); return true;
    case 'End': { const r = run(); if (r) setStep(r.steps.length - 1); return true; }
    case 'Escape': S.selection = null; S.pasting = null; S.preview = null; redraw(); return true;
    case 'Delete': case 'Backspace': if (S.selection) { e.preventDefault(); fillSel(); } return true;
    default: return false;
  }
}
export function keyup(e) { if (e.key === ' ') { S.space = false; boardWrap.classList.remove('pan'); } }

// ---- playing it in the desktop game: its level, every run from its door
export async function play(bot) {
  const g = cur();
  if (!g) return;
  const lv = levelOf(g);
  if (!lv) return toast(`The ${titled(g.name)} is in no level yet: put it in one (Levels → a level → Gauntlet) to play it`, 'bad');
  const st = mazeStart(g), start = st ? { col: st.c, row: st.b, dir: st.dir < 0 ? -1 : 1 } : null, where = st ? startLabel(lv) : 'from its door';
  try {
    await api('POST', '/api/play', { level: lv.number, levelText: lv.text, grids: unsavedGrids(), bot, atGauntlet: !st, start });
    toast(bot ? `The desktop game is opening on level ${lv.number}: the bot plays this grid, every run ${where}.` : `The desktop game is opening on level ${lv.number}, every run ${where}: tap (or Space) to start, Space lays an egg.`, 'ok');
  } catch (e) { toast(e.message, 'bad'); }
}

// ---- switching gauntlets, and the view in and out
export function select(name) {
  const g = store.gauntlets.find((x) => x.name === name);
  if (!g) return false;
  if (S.cur && S.cur !== g) S.cur.view = { z: view.z, ox: view.ox, oy: view.oy, step: R.step };
  const fit = S.cur !== g || !g.view;
  S.cur = g;
  S.selection = null; S.pasting = null; S.preview = null; R.playing = false;
  local.set('cuckoo-gauntlet-current', name);
  view.rows = g.rows;
  if (fit) { if (g.view) { view.z = g.view.z; view.ox = g.view.ox; view.oy = g.view.oy; R.step = g.view.step; } else { view.resize(); view.fit(g.rows); R.step = 0; } }
  if (!fresh(g)) scheduleCheck(g, 0);
  if (!gameFresh(g) && !g.gameBusy) scheduleGame(g, 50);
  renderTabs(); renderPanels(); renderTransport(); renderTrack(); renderReadout(); renderHover(); redraw();
  return true;
}
export function enter() {
  S.active = true;
  view.resize();
  renderTools(); renderChips(); renderTabs(); renderPanels(); renderTransport(); renderTrack(); renderReadout(); renderHover(); redraw();
}
export function leave() { S.active = false; R.playing = false; S.hover = null; }
export function resize() { if (!S.active) return; view.resize(); renderTrack(); redraw(); }
export function init(kept = new Map()) { // (kept: the histories this browser kept, history.js)
  for (const g of store.gauntlets) { restoreHistory(g, kept.get('gauntlet:' + g.name)); g.lints = G.lints(g.rows); diffSaved(g); scheduleCheck(g, 0); }
}
export function saved(g) { // (Save wrote it: the saved state marked on its history)
  g.saved = { rows: g.rows, description: g.description.slice() };
  g.hist = H.markSaved(g.hist);
  diffSaved(g);
  storeDraft(g); keepHistory(g);
}

// ---- the buttons and fields of its own
$('zoomIn').onclick = () => { view.zoomAt(view.w / 2, view.h / 2, 1.25); redraw(); };
$('zoomOut').onclick = () => { view.zoomAt(view.w / 2, view.h / 2, 1 / 1.25); redraw(); };
$('zoomFit').onclick = () => { if (cur()) { view.fit(cur().rows); redraw(); } };
$('rpPlay').onclick = playPause;
$('rpPrev').onclick = () => { R.playing = false; renderTransport(); setStep(R.step - 1); };
$('rpNext').onclick = () => { R.playing = false; renderTransport(); setStep(R.step + 1); };
$('description').addEventListener('focus', () => { S.notesBefore = cur() ? cur().description : null; });
$('description').addEventListener('input', (e) => {
  const g = cur();
  if (!g) return;
  g.description = e.target.value.split('\n');
  storeDraft(g); renderTabs(); emit('top');
});
$('description').addEventListener('change', () => { // (left: one edit on the history)
  const g = cur();
  if (!g || !S.notesBefore || S.notesBefore.join('\n') === g.description.join('\n')) return;
  g.hist = H.record(g.hist, stateOf(g.rows, g.description), 'Description edited', null, 'notes');
  S.notesBefore = g.description;
  keepHistory(g); renderPanels(); emit('top');
});

export const debug = { view, state: S, replay: R, grid: G, select, commit };
