// The level editor's level view: a campaign level's whole course, edited as the game lays it (assets/levels, LevelData.h):
// its walls (how high, their kind), barriers, saws, disco balls, finish line, difficulty band, the droid's raids, and the
// maze gauntlet it plays (which, where: a gauntlet moves between levels here, and a new one starts here too). The
// level's checker judges every edit (build/level_check: the egg budget, notes), and the real game's bot plays the whole
// level a moment after; its run replays along the course.
import * as K from './course.js';
import * as G from './grid.js';
import { CourseView, RULER_LEFT, RULER_TOP, ZONE_LOOK, K_COLORS } from './courseview.js';
import { $, el, esc, titled, clamp, api, toast, local, plural } from './util.js';
import { ask } from './dialog.js';
import { store, on, emit, levelNumbered, levelLaying, gauntletNamed, gauntletOfLevel, gridInfo, fitted, levelDirty, unsavedGrids } from './store.js';
import * as H from './history.js';
import * as D from './describe.js';
import { renderHistory } from './historycard.js';
import { TIPS, tipAttrs, setTip } from './tips.js';
import { showAt, hide as hideTip } from './tooltip.js';
import { LEVEL_TOOLS, KINDS, LEVEL_OVERLAYS } from './tools.js';

const U = K.U;
const SPEEDS = [0.5, 1, 2, 4];
const CLEAR_WITHIN = 150;          // (GameDev_test: every level cleared by the bot within 150 s)
const NEAR_FROM = 11, NEAR_MIN = 3; // (...and from level 11, 3 close calls at least)
const TOOLS = LEVEL_TOOLS, OVERLAYS = LEVEL_OVERLAYS; // (tools.js)

const S = {
  cur: null, active: false,
  tool: 'wall', kind: 'crate',
  overlays: { bot: true, notes: true, changes: true, grid: true },
  hover: null, preview: null, selection: null, clipboard: null, drag: null, space: false, selected: null, hoverSpan: null, hoverFinish: false,
  hoverStart: false, focus: null, notesBefore: null, hazard: null,
};
const R = { i: 0, playing: false, speed: 1, last: 0, follow: true };

const canvas = $('course'), wrap = $('courseWrap');
const view = new CourseView(canvas, $('lminimap'));

// ---- levels: what's saved, what's edited, what the checker and the game's bot last said about it
export function level(n, text, savedText = text) {
  return { number: n, L: K.parseLevel(text), text, saved: { text: savedText, L: K.parseLevel(savedText) }, hist: H.timeline(savedText, 'As saved'),
    check: null, checkKey: '', game: null, gameKey: '', gameBusy: false, changed: null, diff: null, view: null, start: loadStart(n) };
}
const cur = () => S.cur;
const zoneOf = (lv) => K.zoneOf(lv.number);
// what a check is of: the text, and its gauntlet's grid as edited (the bot plays it too)
const checkKeyOf = (lv) => { const g = gauntletOfLevel(lv); return lv.text + (g ? '\n@' + g.name + '\n' + g.rows.join('\n') : ''); };
const fresh = (lv) => !!lv.check && lv.checkKey === checkKeyOf(lv);
const gameFresh = (lv) => lv.gameKey === checkKeyOf(lv) && !lv.gameBusy;

// what's changed since the save (describe.js: the columns lined up, saws and disco balls followed along)
function diffSaved(lv) { lv.diff = D.diffCourse(lv.saved.L, lv.L); lv.changed = lv.diff.changed; }

// unsaved edits survive a reload (this browser only): kept against the level they started from
const draftKey = (lv) => 'cuckoo-level-draft:' + lv.number;
function storeDraft(lv) { local.set(draftKey(lv), levelDirty(lv) ? { text: lv.text, base: lv.saved.text } : null); }
export function restoreDraft(lv) {
  const d = local.get(draftKey(lv));
  if (d && d.base === lv.saved.text && typeof d.text === 'string') {
    lv.text = d.text; lv.L = K.parseLevel(d.text);
    lv.hist = H.record(lv.hist, d.text, 'Unsaved edits, kept in this browser', null, 'draft');
    return true;
  }
  return false;
}
// its history from this browser (history.js): kept if it grew from this saved level and ends where the level is now
export function restoreHistory(lv, kept) {
  if (!kept || kept.base !== lv.saved.text || !kept.t || !Array.isArray(kept.t.states) || kept.t.states[kept.t.cursor] !== lv.text) return false;
  lv.hist = kept.t;
  return true;
}
const keepHistory = (lv) => H.keep('level:' + lv.number, { base: lv.saved.text, t: lv.hist });

// ---- edits
function refresh(lv, { live = false } = {}) {
  lv.text = K.levelText(lv.L, lv.number);
  diffSaved(lv);
  if (!live) { storeDraft(lv); keepHistory(lv); checkStart(lv); }
  emit('level', lv);
  scheduleCheck(lv);
  scheduleGame(lv);
  if (lv === cur()) { if (!live) renderPanels(); redraw(); }
  renderNav();
  if (!live) emit('top');
}
// An edit: the course as the editor keeps it (every span as wide as its grid), on its history with what it did (kind:
// the tool's; hint: the columns it worked on, describe.js)
export function commitTo(lv, L, kind = 'edit', { snapshot = null, hint = null } = {}) {
  L = fitted(L);
  const before = snapshot || lv.L, text = K.levelText(L, lv.number);
  if (text === K.levelText(before, lv.number)) { if (L !== lv.L) { lv.L = L; refresh(lv, { live: true }); } return false; }
  const { label, focus } = D.describeLevel(kind, before, L, hint);
  lv.hist = H.record(lv.hist, text, label, focus, kind);
  lv.L = L;
  refresh(lv);
  return true;
}
export const commit = (L, kind, opts) => cur() && commitTo(cur(), L, kind, opts);
function live(L) { const lv = cur(); if (!lv || L === lv.L) return; lv.L = L; refresh(lv, { live: true }); }
// along its history: undo, redo, or straight to just after an edit
function goTo(lv, t) {
  if (t === lv.hist) return;
  lv.hist = t;
  lv.L = fitted(K.parseLevel(H.current(t)));
  if (lv === cur()) { S.selection = null; S.selected = null; S.focus = null; }
  refresh(lv);
}
export function undo() { const lv = cur(); if (lv && H.canUndo(lv.hist)) goTo(lv, H.undo(lv.hist)); }
export function redo() { const lv = cur(); if (lv && H.canRedo(lv.hist)) goTo(lv, H.redo(lv.hist)); }
export function jumpTo(lv, i) { goTo(lv, H.jump(lv.hist, i)); }
// back to the saved level, as an edit of its own (the shell asks first: Revert lists every unsaved edit); undo
// brings the edits back
export function revertLevel(lv) { if (levelDirty(lv)) commitTo(lv, K.parseLevel(lv.saved.text), 'revert'); }
// what's changed since the save, in a few words (the Revert list, the History card)
export function changeSummary(lv) { return (lv.diff && D.summarizeCourse(lv.diff)) || (levelDirty(lv) ? 'edited' : ''); }
export const canUndo = () => !!(cur() && H.canUndo(cur().hist));
export const canRedo = () => !!(cur() && H.canRedo(cur().hist));
export const undoLabel = () => { const e = cur() && H.nextUndo(cur().hist); return e ? e.label : ''; };
export const redoLabel = () => { const e = cur() && H.nextRedo(cur().hist); return e ? e.label : ''; };
export const current = () => cur();
export function saved(lv) { // (Save wrote it: the saved state marked on its history)
  if (H.current(lv.hist) !== lv.text) lv.hist = H.record(lv.hist, lv.text, 'Refitted to its gauntlet’s grid', null, 'refit');
  lv.hist = H.markSaved(lv.hist);
  lv.saved = { text: lv.text, L: K.parseLevel(lv.text) };
  diffSaved(lv); storeDraft(lv); keepHistory(lv);
  renderNav();
  if (lv === cur()) { renderPanels(); redraw(); }
}

// a new gauntlet thrown away (never saved): out of the level laying it
on('discard-gauntlet', (g) => {
  const lv = levelLaying(g.name);
  if (lv) commitTo(lv, K.removeGauntlet(lv.L, g.name), 'gauntlet');
});

// a gauntlet's grid changed (the gauntlet view): its level's span as wide as it, and judged again with it
on('grid', (g) => {
  const lv = levelLaying(g.name);
  if (!lv) return;
  const L = fitted(lv.L);
  if (L !== lv.L) { lv.L = L; refresh(lv, { live: true }); storeDraft(lv); }
  checkStart(lv);
  scheduleCheck(lv, 600);
  scheduleGame(lv, 2600);
  renderNav();
});

// ---- the play-test's start (Play, Watch the bot): on the course { x }, or in its maze { gauntlet, c, b, dir }; kept per
// level in this browser (a testing aid: not part of the level)
function loadStart(n) { const s = local.get('cuckoo-level-start:' + n); return s && (typeof s.x === 'number' || typeof s.gauntlet === 'string') ? s : null; }
export function setStart(lv, st) {
  lv.start = st;
  local.set('cuckoo-level-start:' + lv.number, st);
  emit('start', lv);
  if (lv === cur()) redraw();
}
export const clearStart = (lv) => setStart(lv, null);
// still a place she can start, after an edit? (snapped to the nearest that is, or cleared)
function checkStart(lv) {
  const st = lv.start;
  if (!st) return;
  if (st.x !== undefined) {
    const x = K.startOnCourse(lv.L, st.x);
    if (x === null) { setStart(lv, null); toast(`Level ${lv.number}’s start point is in a gauntlet now: cleared (Play starts at the beginning)`, 'bad'); }
    else if (x !== st.x) setStart(lv, { x });
    return;
  }
  const g = gauntletNamed(st.gauntlet), sp = K.spanOf(lv.L, st.gauntlet);
  const ok = g && sp && G.standable(g.rows, st.c, G.index(g.rows, st.b));
  if (!ok || ok.b !== st.b) { setStart(lv, null); toast(`Level ${lv.number}’s start in the ${titled(st.gauntlet)} is no place to stand now: cleared`, 'bad'); }
}
// where it is, in a few words (the chip by Play)
export function startLabel(lv) {
  const st = lv && lv.start;
  if (!st) return '';
  return st.x !== undefined ? `from ${st.x} m` : `from the ${titled(st.gauntlet)}, col ${st.c} row ${st.b} ${st.dir < 0 ? '←' : '→'}`;
}
export const startOf = (lv) => (lv ? lv.start : null);
// what Play sends the server (server.py play_args)
export function startRequest(lv) {
  const st = lv && lv.start;
  if (!st) return null;
  return st.x !== undefined ? { x: st.x } : { col: st.c, row: st.b, dir: st.dir < 0 ? -1 : 1 };
}

// ---- checks: the level's checker after every edit; the real game's bot playing the level a moment after the edits stop
const timers = { check: new Map(), game: new Map() };
function request(lv, game) {
  const g = gauntletOfLevel(lv), grids = unsavedGrids();
  return { level: lv.number, text: lv.text, game, grids: g && grids[g.name] ? { [g.name]: grids[g.name] } : {} };
}
export function scheduleCheck(lv, delay = 140) {
  clearTimeout(timers.check.get(lv.number));
  timers.check.set(lv.number, setTimeout(() => runCheck(lv), delay));
}
async function runCheck(lv) {
  const k = checkKeyOf(lv);
  const g = gauntletOfLevel(lv);
  if (g && !G.doors(g.rows)) { lv.check = { ok: false, error: `the ${g.name}’s grid has no doors to lay it by` }; lv.checkKey = k; return afterCheck(lv); }
  let res;
  try { res = await api('POST', '/api/levels/check', request(lv, false)); } catch (e) { res = { ok: false, error: e.message }; }
  if (checkKeyOf(lv) !== k) return;
  lv.check = res; lv.checkKey = k;
  afterCheck(lv);
}
function afterCheck(lv) {
  if (lv === cur() && S.active) { renderPanels(); redraw(); renderTrack(); }
  renderNav(); renderPicker();
  emit('level-checked', lv);
  setTimeout(backgroundGames, 400);
}
export function scheduleGame(lv, delay = 1600) {
  clearTimeout(timers.game.get(lv.number));
  lv.gameBusy = true;
  timers.game.set(lv.number, setTimeout(() => runGame(lv), delay));
}
async function runGame(lv) {
  const k = checkKeyOf(lv);
  let res;
  try { res = await api('POST', '/api/levels/check', request(lv, true)); } catch (e) { res = { ok: false, error: e.message }; }
  if (bg === lv) { bg = null; setTimeout(backgroundGames, 250); }
  if (res.cancelled || checkKeyOf(lv) !== k) return;
  lv.game = res.ok ? res.game : { error: res.error }; lv.gameKey = k; lv.gameBusy = false;
  if (lv === cur() && S.active) { clampReplay(); renderPanels(); redraw(); renderTrack(); renderReadout(); }
  renderNav(); renderPicker();
  emit('level-checked', lv);
}
// every level's bot run, one at a time in the background (this one first), so every level's state is known
let bg = null;
export function backgroundGames() {
  if (bg) return;
  const next = [cur(), ...store.levels].find((lv) => lv && !lv.gameBusy && !gameFresh(lv) && fresh(lv) && lv.check && lv.check.ok);
  if (!next) return;
  bg = next;
  clearTimeout(timers.game.get(next.number));
  next.gameBusy = true;
  runGame(next);
}

// What the tests want of a level (LevelData_test, GameDev_test, GameGauntlet_test, Campaign_test), item by item; and
// what reads unfairly (notes: no test fails on those)
export function verdict(lv, gauntletVerdict) {
  if (!lv.check || !fresh(lv)) return { state: 'busy', items: [] };
  const ch = lv.check, items = [];
  if (!ch.ok) return { state: 'bad', items: [{ ok: false, what: 'A valid level', why: ch.error || 'it doesn’t parse' }] };
  items.push({ ok: true, what: 'A valid level', why: `${Math.round(ch.length)} m, ${plural(ch.walls, 'wall')}, ${plural(ch.saws, 'saw')}, ${plural(ch.discos, 'disco ball')}` });
  const gm = gameFresh(lv) ? lv.game : null;
  if (!gm) items.push({ busy: true, what: 'The game’s bot clears it', why: 'playing it in the real game…' });
  else if (gm.error) items.push({ ok: false, what: 'The game’s bot clears it', why: gm.error });
  else {
    const ok = gm.cleared && gm.seconds <= CLEAR_WITHIN;
    items.push({ ok, what: 'The game’s bot clears it', why: gm.cleared ? `in ${gm.seconds.toFixed(1)} s${ok ? '' : ` (the tests give it ${CLEAR_WITHIN})`}` : `it crashed at ${gm.x.toFixed(0)} m`, x: gm.cleared ? null : gm.x });
    if (lv.number >= NEAR_FROM) items.push({ ok: gm.closeCalls >= NEAR_MIN, what: `${NEAR_MIN}+ close calls`, why: `${gm.closeCalls} on the bot’s run (and ${gm.perfects} PERFECT)` });
  }
  const sp = K.spans(lv.L);
  for (const s of sp) {
    const twice = store.levels.filter((o) => o !== lv && o.L.cols.some((k) => k.g === s.name)).map((o) => o.number);
    if (twice.length) items.push({ ok: false, what: 'No gauntlet in two levels', why: `the ${s.name} is in level ${twice.join(', ')} too: take it out of one`, level: twice[0] });
  }
  for (const d of ch.doors || []) items.push({ ok: false, what: 'Its gauntlet’s door on its course', why: d.text, x: d.x });
  if (sp.length) {
    const g = gauntletNamed(sp[0].name), gv = g && gauntletVerdict ? gauntletVerdict(g) : null;
    if (gv) items.push({ ok: gv.state === 'bad' ? false : gv.state === 'ok' ? true : undefined, busy: gv.state === 'busy', what: `Its gauntlet, the ${titled(g.name)}`, why: gv.state === 'ok' ? 'ready to ship' : gv.state === 'bad' ? `not ready: ${gv.items.filter((it) => it.ok === false).map((it) => it.what.toLowerCase()).join('; ')}` : 'checking…', gauntlet: g.name });
  }
  if (ch.short.length) items.push({ warn: true, what: 'The egg budget', why: `${plural(ch.short.length, 'place')} a careful player runs short: ${ch.short[0].text}`, x: ch.short[0].x });
  else items.push({ ok: true, what: 'The egg budget', why: `every climb and blade affordable (${ch.lowest.toFixed(1)} eggs at the least)` });
  if (ch.notes.length) items.push({ warn: true, what: plural(ch.notes.length, 'note'), why: ch.notes[0].text, x: ch.notes[0].x });
  const bad = items.some((it) => it.ok === false), busy = items.some((it) => it.busy);
  return { state: bad ? 'bad' : busy ? 'busy' : 'ok', items };
}
let gauntletVerdict = null; // (the gauntlet view's, handed over by the shell)
export function setGauntletVerdict(fn) { gauntletVerdict = fn; }

// ---- the top bar: this level, the levels either side, the picker of them all
export function renderNav() {
  const nav = $('lnav');
  const lv = cur();
  if (!lv || !S.active) return;
  const v = verdict(lv, gauntletVerdict), g = gauntletOfLevel(lv), z = zoneOf(lv);
  const prev = levelNumbered(lv.number - 1), next = levelNumbered(lv.number + 1);
  nav.innerHTML = `
    <button class="icon" id="lvPrev" aria-label="The level before"${tipAttrs('level.prev')} ${prev ? '' : 'disabled'}><svg viewBox="0 0 24 24"><path d="M15 6l-6 6 6 6"/></svg></button>
    <button class="lv-current zone-${z}" id="lvPick" aria-label="Every level"${tipAttrs('level.picker')} aria-haspopup="true">
      <span class="state ${v.state}"></span><span class="num">${lv.number}</span>
      <span class="meta"><span class="zone">${esc(K.ZONES[z].name)}</span><span class="gname">${g ? `the ${esc(titled(g.name))}` : 'no gauntlet'}</span></span>
      ${levelDirty(lv) ? '<span class="unsaved"></span>' : ''}
      <svg class="chev" viewBox="0 0 24 24"><path d="M6 9l6 6 6-6"/></svg>
    </button>
    <button class="icon" id="lvNext" aria-label="The level after"${tipAttrs('level.next')} ${next ? '' : 'disabled'}><svg viewBox="0 0 24 24"><path d="M9 6l6 6-6 6"/></svg></button>`;
  $('lvPrev').onclick = () => prev && emit('show-level', prev.number);
  $('lvNext').onclick = () => next && emit('show-level', next.number);
  $('lvPick').onclick = (e) => { e.stopPropagation(); togglePicker(); };
}
function skyline(lv, w, h) { // (a level in miniature: its walls, its gauntlet's span)
  const cv = el('canvas');
  cv.width = w * 2; cv.height = h * 2;
  const ctx = cv.getContext('2d');
  ctx.scale(2, 2);
  const L = lv.L, n = Math.max(1, Math.ceil(L.length / U)), look = ZONE_LOOK[zoneOf(lv)];
  const bins = new Array(w).fill(0), gold = new Array(w).fill(false);
  for (let c = 0; c < Math.min(n, L.cols.length); c++) {
    const b = Math.min(w - 1, Math.floor((c / n) * w)), k = L.cols[c];
    if (k.g) gold[b] = true; else bins[b] = Math.max(bins[b], k.h);
  }
  const top = Math.max(8, ...bins);
  for (let b = 0; b < w; b++) {
    if (gold[b]) { ctx.fillStyle = 'rgba(255,194,58,0.85)'; ctx.fillRect(b, 1, 1, h - 1); continue; }
    const hh = (bins[b] / top) * (h - 2);
    ctx.fillStyle = look.accent; ctx.globalAlpha = 0.75; ctx.fillRect(b, h - hh, 1, hh); ctx.globalAlpha = 1;
  }
  return cv;
}
let pickerOpen = false;
function togglePicker(open = !pickerOpen) { pickerOpen = open; renderPicker(); }
export function renderPicker() {
  const p = $('picker');
  p.classList.toggle('open', pickerOpen && S.active);
  if (!pickerOpen || !S.active) return;
  p.innerHTML = '';
  const zones = [];
  for (const lv of store.levels) {
    const z = zoneOf(lv);
    if (!zones.length || zones[zones.length - 1].zone !== z || zones[zones.length - 1].levels.length >= 10) zones.push({ zone: z, levels: [] });
    zones[zones.length - 1].levels.push(lv);
  }
  for (const zr of zones) {
    const row = el('div', `prow zone-${zr.zone}`);
    row.append(el('div', 'pzone', `<b>${esc(K.ZONES[zr.zone].short)}</b><span>${zr.levels[0].number}–${zr.levels[zr.levels.length - 1].number}</span>`));
    const tiles = el('div', 'ptiles');
    for (const lv of zr.levels) {
      const v = verdict(lv, gauntletVerdict), g = gauntletOfLevel(lv);
      const t = el('button', 'ptile' + (lv === cur() ? ' on' : ''), `<span class="state ${v.state}"></span><span class="pnum">${lv.number}</span>${levelDirty(lv) ? '<span class="unsaved"></span>' : ''}`);
      t.append(skyline(lv, 58, 16));
      if (g) t.append(el('span', 'pg', esc(titled(g.name))));
      t.dataset.tipTitle = `Level ${lv.number}${g ? ` · the ${titled(g.name)}` : ''}`;
      t.dataset.tip = `${v.state === 'ok' ? 'Ready to ship.' : v.state === 'bad' ? 'Not ready: its checks fail.' : 'Checking…'}${levelDirty(lv) ? ' Unsaved edits.' : ''} ${D.summarizeCourse(lv.diff || D.diffCourse(lv.saved.L, lv.L)) || ''}`.trim();
      t.dataset.tipSide = 'below';
      t.onclick = () => { togglePicker(false); emit('show-level', lv.number); };
      tiles.append(t);
    }
    row.append(tiles);
    p.append(row);
  }
}
document.addEventListener('pointerdown', (e) => { if (pickerOpen && !e.target.closest('#picker') && !e.target.closest('#lvPick')) togglePicker(false); });

// ---- the tools column
function kindSwatch(kind) {
  const cv = el('canvas');
  cv.width = cv.height = 56;
  const ctx = cv.getContext('2d');
  ctx.scale(2, 2);
  const look = ZONE_LOOK[cur() ? zoneOf(cur()) : 'city'];
  ctx.fillStyle = '#0c0816'; ctx.fillRect(0, 0, 28, 28);
  const fake = { ctx, z: 13 };
  for (let j = 0; j < 2; j++) for (let i = 0; i < 2; i++) view.block.call(fake, kind === 'hay' || (kind === 'mix' && j === 1) ? 'hay' : 'crate', 1 + i * 13, 27 - (j + 1) * 13, look, i, j);
  return cv;
}
function renderTools() {
  const t = $('ltools');
  t.innerHTML = '';
  t.append(el('h4', null, 'Walls'));
  const sw = el('div', 'swatches');
  for (const k of KINDS) {
    const b = setTip(el('button', 'swatch' + (S.kind === k.id ? ' primary' : '')), k.tip);
    b.setAttribute('aria-label', k.label);
    b.append(kindSwatch(k.id), el('span', 'k', k.key));
    if (S.kind === k.id) b.append(el('span', 'tag', 'ON'));
    b.onclick = () => { S.kind = k.id; renderTools(); };
    sw.append(b);
  }
  t.append(sw, el('div', 'rule'), el('h4', null, 'Tools'));
  for (const tool of TOOLS) {
    const b = setTip(el('button', 'tool' + (S.tool === tool.id ? ' on' : ''), `<svg viewBox="0 0 24 24">${tool.icon}</svg><span>${tool.label}</span>`), tool.tip);
    b.onclick = () => setTool(tool.id);
    t.append(b);
  }
}
function setTool(id) { S.tool = id; S.preview = null; S.selected = null; if (id !== 'select') S.selection = null; renderTools(); redraw(); updateCursor(); }
function renderChips() {
  const c = $('lchips');
  c.innerHTML = '';
  for (const o of OVERLAYS) {
    const b = setTip(el('button', 'chip' + (S.overlays[o.id] ? ' on' : ''), `<span class="dot" style="background:${o.color}"></span>${o.label}`), o.tip);
    b.onclick = () => { S.overlays[o.id] = !S.overlays[o.id]; renderChips(); redraw(); };
    c.append(b);
  }
}

// ---- the inspector
export function renderPanels() {
  const lv = cur();
  if (!lv || !S.active) return;
  const v = verdict(lv, gauntletVerdict);
  const head = v.state === 'ok' ? ['ok', 'Ready to ship', 'Every check the tests make passes.'] : v.state === 'bad' ? ['bad', 'Not ready', 'The tests will fail (and block commits) until these pass.'] : ['busy', 'Checking', 'Its checker, then the real game…'];
  const vd = $('lverdict');
  vd.innerHTML = `<h3>Level ${lv.number} <span class="aside">${esc(K.ZONES[zoneOf(lv)].name)}</span></h3><div class="headline"><span class="badge ${head[0]}">${head[1]}</span><span class="sub">${head[2]}</span></div>`;
  const ul = el('ul', 'checks');
  for (const it of v.items) {
    const cls = it.busy ? 'busy' : it.warn ? 'warn' : it.ok ? 'ok' : 'bad';
    const li = el('li', cls, `<span class="mark">${it.busy ? '…' : it.warn ? '!' : it.ok ? '✓' : '✕'}</span><span><div class="what">${esc(it.what)}</div><div class="why">${esc(it.why)}</div></span>`);
    const link = (tip, fn) => { li.classList.add('link'); li.dataset.tip = tip; li.dataset.tipSide = 'left'; li.onclick = fn; };
    if (it.x !== undefined && it.x !== null) link('Click: show me where, on the course.', () => { view.centerOnX(it.x); redraw(); });
    else if (it.gauntlet) link(`Click: open the ${titled(it.gauntlet)}’s grid.`, () => emit('show-gauntlet', it.gauntlet));
    else if (it.level) link(`Click: open level ${it.level}.`, () => emit('show-level', it.level));
    ul.append(li);
  }
  vd.append(ul);
  setTip(vd.querySelector('h3'), 'card.verdict');
  renderHistoryCard(lv); renderProps(lv); renderNumbers(lv); renderGame(lv); renderNotes(lv);
  const ta = $('ldescription');
  if (document.activeElement !== ta) ta.value = lv.L.notes.join('\n');
}
function renderHistoryCard(lv) {
  const box = $('lhistory');
  renderHistory(box, { t: lv.hist, summary: changeSummary(lv),
    onJump: (i) => { jumpTo(lv, i); emit('top'); },
    onHover: (f) => { S.focus = f; },
    onShow: (f) => { if (f) view.centerOnX((f[0] + f[1]) / 2); } });
  setTip(box.querySelector('h3'), 'card.history');
}
setInterval(() => { if (S.active && cur()) renderHistoryCard(cur()); }, 30000); // (how long ago, kept fresh)
function renderProps(lv, force = false) {
  const p = $('lprops');
  if (!force && p.contains(document.activeElement) && document.activeElement.matches('input, select') && p.dataset.level === String(lv.number)) return; // (mid-edit: kept)
  p.dataset.level = String(lv.number);
  const L = lv.L, ch = fresh(lv) && lv.check && lv.check.ok ? lv.check : null, g = gauntletOfLevel(lv);
  const speed = ch ? (ch.speed[0] === ch.speed[1] ? `${ch.speed[0]} m/s` : `${ch.speed[0]} → ${ch.speed[1]} m/s`) : '…';
  const regen = ch ? (ch.regen[0] === ch.regen[1] ? `${ch.regen[0].toFixed(2)} s` : `${ch.regen[0].toFixed(2)} → ${ch.regen[1].toFixed(2)} s`) : '…';
  const raids = L.raids;
  const options = ['<option value="">None</option>'].concat([...store.gauntlets].sort((a, b) => a.name.localeCompare(b.name)).map((x) => {
    const at = levelLaying(x.name);
    return `<option value="${esc(x.name)}" ${g && g.name === x.name ? 'selected' : ''}>${esc(titled(x.name))}${at ? (at === lv ? '' : ` (level ${at.number})`) : ' (in no level)'}</option>`;
  }));
  p.innerHTML = `<h3>The level</h3>
    <div class="prop"><label for="lpLength"${tipAttrs('prop.finish')}>Finish line</label><div class="ctl"><input id="lpLength" type="number" min="${K.MIN_LENGTH}" step="1" value="${+L.length.toFixed(1)}"${tipAttrs('prop.finish')}><span class="unit">m</span></div></div>
    <div class="prop"><label${tipAttrs('prop.difficulty')}>Difficulty</label>
      <div class="ctl"><input id="lpOff" type="number" step="5" value="${+(L.diffOffset ?? K.defaultOffset(lv.number)).toFixed(2)}"${tipAttrs('prop.difficulty', 'This one: where on the curve it starts.')}><span class="unit">+</span><input id="lpScale" type="number" step="0.05" min="0" value="${+L.diffScale.toFixed(3)}"${tipAttrs('prop.difficulty', 'This one: how much of the curve each metre climbs.')}><span class="unit">× m</span></div></div>
    <div class="prop sub"><span></span><span class="readout-line">speed ${esc(speed)} · an egg every ${esc(regen)}</span></div>
    <div class="prop"><label${tipAttrs('prop.raids')}>Droid raids</label>
      <div class="seg" id="lpRaids"${tipAttrs('prop.raids')}>${['auto', 'none', 1, 2, 3].map((r) => `<button data-r="${r}" class="${String(raids) === String(r) ? 'on' : ''}">${r === 'auto' ? 'Auto' : r === 'none' ? 'None' : r}</button>`).join('')}</div></div>
    <div class="prop"><label for="lpGauntlet"${tipAttrs('prop.gauntlet')}>Gauntlet</label><div class="ctl"><select id="lpGauntlet"${tipAttrs('prop.gauntlet')}>${options.join('')}</select></div></div>
    <div class="prop sub"><span></span><span class="btns"><button class="small" id="lpEdit"${tipAttrs('prop.edit')} ${g ? '' : 'disabled'}>Edit its grid</button><button class="small" id="lpNew"${tipAttrs('prop.new')}>New gauntlet…</button></span></div>
    <div class="prop"><label>Zone</label><span class="zone-tag zone-${zoneOf(lv)}">${esc(K.ZONES[zoneOf(lv)].name)}</span></div>`;
  const num = (id, fn) => { $(id).onchange = (e) => { const v = parseFloat(e.target.value); if (Number.isFinite(v)) fn(v); else renderProps(lv); }; };
  num('lpLength', (v) => commit(K.setLength(cur().L, v), 'finish'));
  num('lpOff', (v) => commit({ ...cur().L, diffOffset: Math.round(v * 1000) / 1000 }, 'difficulty'));
  num('lpScale', (v) => commit({ ...cur().L, diffScale: Math.max(0, Math.round(v * 1000) / 1000) }, 'difficulty'));
  for (const b of $('lpRaids').querySelectorAll('button')) b.onclick = () => { const r = b.dataset.r; commit({ ...cur().L, raids: r === 'auto' || r === 'none' ? r : Number(r) }, 'raids'); };
  $('lpGauntlet').onchange = (e) => assignGauntlet(e.target.value);
  $('lpEdit').onclick = () => g && emit('show-gauntlet', g.name);
  $('lpNew').onclick = () => emit('new-gauntlet', cur().number);
}
function renderNumbers(lv) {
  const n = $('lnumbers'), ch = lv.check;
  if (!ch || !ch.ok) { n.innerHTML = '<h3>The course</h3><p class="hint">Numbers come with a valid level.</p>'; return; }
  n.innerHTML = `<h3>The course <span class="aside">${fresh(lv) ? '' : 'updating…'}</span></h3>
    <div class="stats">
      <div class="stat"><div class="v">${Math.round(ch.length)}<small>m</small></div><div class="l">To the finish</div></div>
      <div class="stat"><div class="v">${ch.walls}</div><div class="l">Walls · ${ch.barriers} barriers</div></div>
      <div class="stat"><div class="v">${ch.tallest}</div><div class="l">Tallest wall</div></div>
      <div class="stat"><div class="v">${ch.saws}<small>/ ${ch.discos}</small></div><div class="l">Saws / disco balls</div></div>
    </div>
    <dl class="kv"><dt>The least a careful player holds</dt><dd>${ch.lowest.toFixed(1)} eggs</dd>
    ${ch.gauntlets.map((g) => `<dt>The ${esc(titled(g.name))}</dt><dd>${Math.round(g.x0)}–${Math.round(g.x0 + g.cols * U)} m, door row ${g.entryRow}</dd>`).join('')}
    <dt>Droid raids</dt><dd>${ch.raids < 0 ? 'the game’s rule' : ch.raids === 0 ? 'none' : ch.raids}</dd></dl>`;
}
function renderGame(lv) {
  const c = $('lgame');
  const gm = gameFresh(lv) ? lv.game : null;
  let line;
  if (fresh(lv) && lv.check && !lv.check.ok) line = '<span class="big bad">No run</span><span class="game-sub">the level isn’t valid</span>';
  else if (!gm) line = '<span class="big busy">Playing…</span><span class="game-sub">the real game, headless, the bot playing the level</span>';
  else if (gm.error) line = `<span class="big bad">No run</span><span class="game-sub">${esc(gm.error)}</span>`;
  else if (gm.cleared) line = `<span class="big ok">Cleared</span><span class="game-sub">in ${gm.seconds.toFixed(1)} s · ${gm.perfects} PERFECT, ${gm.closeCalls} CLOSE SHAVE / CALL</span>`;
  else line = `<span class="big bad">Crashed</span><span class="game-sub">at ${gm.x.toFixed(0)} m, ${gm.seconds.toFixed(1)} s in · <a href="#" id="lgCrash">show me</a></span>`;
  c.innerHTML = `<h3>In the game <span class="aside">the bot, headless</span></h3><div class="game-line">${line}</div>`;
  const a = $('lgCrash');
  if (a) a.onclick = (e) => { e.preventDefault(); setReplay(lv.game.trace.length - 1); };
}
function renderNotes(lv) {
  const c = $('lnotes'), ch = lv.check;
  const all = ch && ch.ok ? [...(ch.doors || []).map((n) => ({ ...n, kind: 'bad' })), ...ch.short.map((n) => ({ ...n, kind: 'bad' })), ...ch.notes.map((n) => ({ ...n, kind: 'note' }))] : [];
  c.hidden = !all.length;
  if (!all.length) return;
  c.innerHTML = `<h3>Notes <span class="aside">${all.length}</span></h3>`;
  const ul = el('ul', 'notes');
  for (const n of all.slice(0, 40)) {
    const li = el('li', n.kind === 'bad' ? 'bad' : '', `${esc(n.text)} <span style="color:var(--faint)">(${n.x.toFixed(1)} m)</span>`);
    li.onclick = () => { view.centerOnX(n.x); redraw(); };
    ul.append(li);
  }
  c.append(ul);
}

// A level's gauntlet: one of the game's, moved here from the level laying it (its span there street again), or none
async function assignGauntlet(name, lv = cur()) {
  if (!lv) return;
  const here = K.spans(lv.L)[0];
  if (!name) { if (here) commitTo(lv, K.removeGauntlet(lv.L, here.name), 'gauntlet'); return; }
  if (here && here.name === name) return;
  const info = gridInfo(name);
  if (!info) { toast(`The ${titled(name)}’s grid needs both its doors before a level can lay it`, 'bad'); return renderProps(lv, true); }
  const other = levelLaying(name);
  if (other && other !== lv && !(await ask({ title: `Move the ${titled(name)} to level ${lv.number}?`, ok: `Move it to level ${lv.number}`,
    body: `It’s in level ${other.number} now. Level ${other.number} then has no gauntlet: its span and approach there become street. Both levels are unsaved till you Save (it saves them together); Undo on each takes it back.` }))) return renderProps(lv, true);
  let L = lv.L, at = Math.round((0.4 * L.length) / U);
  if (here) { at = here.c0; L = K.removeGauntlet(L, here.name); }
  const placed = K.placeGauntlet(L, name, info, at);
  if (placed === L) { toast('No room for it there', 'bad'); return renderProps(lv, true); }
  if (other && other !== lv) commitTo(other, K.removeGauntlet(other.L, name), 'gauntlet');
  commitTo(lv, placed, 'gauntlet');
  if (lv === cur()) view.centerOnX(K.spanOf(lv.L, name).c0 * U);
  toast(other && other !== lv ? `The ${titled(name)} moved here from level ${other.number} (both unsaved: Save saves them together)` : `The ${titled(name)} is in level ${lv.number} now, at ${Math.round(at * U)} m: its forecourt and porch laid to its doors`, 'ok');
}
// a new gauntlet (the gauntlet view made it): into this level, where its gauntlet was or at 40%
export function adoptGauntlet(n, name) {
  const lv = levelNumbered(n);
  if (lv) return assignGauntlet(name, lv);
}

// ---- the replay: the bot's run in the real game, along the course
const trace = () => { const lv = cur(); return lv && gameFresh(lv) && lv.game && lv.game.trace && lv.game.trace.length ? lv.game.trace : null; };
const SAMPLE = 5 / 60; // (level_check: a trace point every 5 frames)
function clampReplay() { const tr = trace(); R.i = tr ? clamp(R.i, 0, tr.length - 1) : 0; }
function replayPoint() {
  const tr = trace();
  if (!tr) return null;
  const i = Math.floor(R.i), f = R.i - i, a = tr[i], b = tr[Math.min(tr.length - 1, i + 1)];
  return { x: a[0] + (b[0] - a[0]) * f, base: a[1] + (b[1] - a[1]) * f, eggs: f < 0.5 ? a[2] : b[2], i };
}
function setReplay(i, follow = true) {
  const tr = trace();
  if (!tr) return;
  R.i = clamp(i, 0, tr.length - 1);
  if (follow) { const p = replayPoint(); if (!view.onScreen(p.x)) view.centerOnX(p.x); }
  renderReadout(); renderTrack(); redraw();
}
function playPause() {
  const tr = trace();
  if (!tr) return;
  if (R.playing) { R.playing = false; renderTransport(); return; }
  if (R.i >= tr.length - 1) R.i = 0;
  R.playing = true; R.last = performance.now();
  renderTransport();
}
function stepReplay(now) {
  if (!R.playing) return;
  const tr = trace();
  if (!tr || !S.active) { R.playing = false; renderTransport(); return; }
  R.i = Math.min(tr.length - 1, R.i + ((now - R.last) / 1000) * R.speed / SAMPLE);
  R.last = now;
  const p = replayPoint();
  if (R.follow) { const px = view.X(p.x / U); if (px > view.w * 0.62 || px < RULER_LEFT + 40) { view.ox -= px - view.w * 0.38; view.clamp(); } }
  renderReadout(); renderTrack();
  if (R.i >= tr.length - 1) { R.playing = false; renderTransport(); }
}
function renderTransport() {
  const sp = $('lrpSpeed');
  sp.innerHTML = '';
  for (const s of SPEEDS) {
    const b = el('button', R.speed === s ? 'on' : '', `${s}×`);
    b.onclick = () => { R.speed = s; renderTransport(); };
    sp.append(b);
  }
  $('lrpPlay').classList.toggle('playing', R.playing);
  $('lrpFollow').classList.toggle('on', R.follow);
}
function renderReadout() {
  const o = $('lreadout'), tr = trace(), lv = cur();
  if (!lv || !tr) { o.innerHTML = `<div class="t">—</div><div class="ev" style="color:var(--muted)">${lv && lv.gameBusy ? 'the bot’s playing it…' : lv && lv.game && lv.game.error ? esc(lv.game.error) : 'no run yet'}</div>`; return; }
  const p = replayPoint(), eggs = p.eggs;
  const near = (lv.game.events || []).filter((e) => Math.abs(e[0] - p.x) < 1.2).map((e) => (e[2] ? '<span style="color:var(--cyan)">CLOSE CALL</span>' : '<span style="color:var(--pink)">PERFECT</span>'));
  const end = Math.floor(R.i) >= tr.length - 1 ? (lv.game.cleared ? '<span style="color:var(--green)">FINISH</span>' : '<span style="color:var(--red)">CRASH</span>') : '';
  const icons = Array.from({ length: Math.max(8, eggs) }, (_, i) => `<i class="${i < eggs ? 'full' : ''}"></i>`).join('');
  o.innerHTML = `<div class="t">${(R.i * SAMPLE).toFixed(1)}s <small>of ${(tr.length * SAMPLE).toFixed(1)} s</small></div>
    <div><div class="l">Where</div>${p.x.toFixed(1)} m</div><div><div class="l">On</div>row ${(p.base / U).toFixed(0)}</div>
    <div style="grid-column:1/-1"><div class="l">Her stack</div><div class="eggs">${icons}</div></div>
    <div class="ev">${[...near, end].filter(Boolean).join(' · ')}</div>`;
}
// the track: along the course, the careful player's meter (the checker's egg budget) and the eggs the bot carried
export function renderTrack() {
  const cv = $('ltrack'), ctx = cv.getContext('2d');
  const rect = cv.getBoundingClientRect(), dpr = window.devicePixelRatio || 1;
  if (!rect.width) return;
  if (cv.width !== Math.round(rect.width * dpr) || cv.height !== Math.round(rect.height * dpr)) { cv.width = Math.round(rect.width * dpr); cv.height = Math.round(rect.height * dpr); }
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  const W = rect.width, H = rect.height, lv = cur();
  ctx.fillStyle = '#0a0612'; ctx.fillRect(0, 0, W, H);
  if (!lv) return;
  const L = lv.L, len = Math.max(30, L.length), top = 14, bot = H - 14;
  const x = (m) => 8 + (m / len) * (W - 16), y8 = (v) => bot - (Math.min(v, 12) / 12) * (bot - top);
  // the course's skyline, faint
  ctx.fillStyle = 'rgba(255,255,255,0.05)';
  for (let c = 0; c < Math.min(L.cols.length, Math.ceil(len / U)); c++) { const k = L.cols[c]; if (k.h && !k.g) ctx.fillRect(x(c * U), bot - (k.h / 16) * (bot - top), Math.max(1, (W - 16) * U / len), (k.h / 16) * (bot - top)); else if (k.g) { ctx.fillStyle = 'rgba(255,194,58,0.1)'; ctx.fillRect(x(c * U), top, Math.max(1, (W - 16) * U / len), bot - top); ctx.fillStyle = 'rgba(255,255,255,0.05)'; } }
  ctx.font = '500 10px "Chakra Petch"'; ctx.textBaseline = 'alphabetic';
  const every = len > 300 ? 50 : 25;
  for (let m = 0; m <= len; m += every) { ctx.fillStyle = 'rgba(255,255,255,0.06)'; ctx.fillRect(x(m), top, 1, bot - top); ctx.fillStyle = 'rgba(236,230,255,0.35)'; ctx.fillText(`${m}`, x(m) + 3, H - 3); }
  const ch = lv.check && lv.check.ok ? lv.check : null;
  if (ch && ch.walk && ch.walk.length) { // the careful player's meter
    ctx.beginPath(); ctx.moveTo(x(0), bot);
    for (const [m, e] of ch.walk) ctx.lineTo(x(m), y8(e));
    ctx.lineTo(x(ch.walk[ch.walk.length - 1][0]), bot); ctx.closePath(); ctx.fillStyle = 'rgba(41,231,255,0.12)'; ctx.fill();
    ctx.strokeStyle = 'rgba(41,231,255,0.8)'; ctx.lineWidth = 1.2; ctx.beginPath();
    ch.walk.forEach(([m, e], i) => (i ? ctx.lineTo(x(m), y8(e)) : ctx.moveTo(x(m), y8(e)))); ctx.stroke();
    for (const s of ch.short) { ctx.fillStyle = K_COLORS.short; ctx.beginPath(); ctx.arc(x(s.x), bot - 3, 3, 0, Math.PI * 2); ctx.fill(); }
  }
  const tr = trace();
  if (tr) { // the eggs the bot carried
    ctx.strokeStyle = 'rgba(244,255,90,0.8)'; ctx.lineWidth = 1.2; ctx.beginPath();
    tr.forEach((p, i) => (i ? ctx.lineTo(x(p[0]), y8(p[2])) : ctx.moveTo(x(p[0]), y8(p[2])))); ctx.stroke();
    for (const [m, , kind] of lv.game.events || []) { ctx.fillStyle = kind ? K_COLORS.call : K_COLORS.perfect; ctx.beginPath(); ctx.arc(x(m), 7, 2.5, 0, Math.PI * 2); ctx.fill(); }
    if (!lv.game.cleared) { const p = tr[tr.length - 1]; ctx.fillStyle = K_COLORS.crash; ctx.fillRect(x(p[0]) - 1, top - 6, 3, bot - top + 6); }
    const p = replayPoint();
    ctx.fillStyle = '#fff'; ctx.fillRect(x(p.x) - 1, 2, 2, H - 4);
    ctx.beginPath(); ctx.moveTo(x(p.x) - 5, 2); ctx.lineTo(x(p.x) + 5, 2); ctx.lineTo(x(p.x), 8); ctx.fill();
  }
  ctx.fillStyle = '#f4ff5a'; ctx.fillRect(x(L.length) - 1, top, 2, bot - top);
  ctx.textAlign = 'right'; ctx.fillStyle = 'rgba(41,231,255,0.75)'; ctx.fillText('a careful player’s meter', W - 8, top + 8); ctx.fillStyle = 'rgba(244,255,90,0.75)'; ctx.fillText('the bot’s stack', W - 8, top + 20); ctx.textAlign = 'left';
}
function trackSeek(e) {
  const tr = trace(), lv = cur();
  if (!lv) return;
  const rect = $('ltrack').getBoundingClientRect();
  const m = ((e.clientX - rect.left - 8) / (rect.width - 16)) * Math.max(30, lv.L.length);
  if (!tr) { view.centerOnX(m); redraw(); return; }
  let best = 0;
  tr.forEach((p, i) => { if (Math.abs(p[0] - m) < Math.abs(tr[best][0] - m)) best = i; });
  R.playing = false; renderTransport();
  setReplay(best);
}

// ---- drawing: the canvas animates (saws spin, disco balls twinkle, the dance floor cycles) while the view's up
let animating = false, lastFrame = 0, failed = false;
export function redraw() { if (!animating && S.active) { animating = true; requestAnimationFrame(loop); } }
function loop(now) {
  if (!S.active) { animating = false; return; }
  requestAnimationFrame(loop); // (first: a frame that throws doesn't stop the animation; a hidden page gets no frames)
  try {
    stepReplay(now);
    if (now - lastFrame > 31) { lastFrame = now; draw(now / 1000); }
  } catch (e) { if (!failed) { failed = true; console.error(e); } }
}
document.addEventListener('visibilitychange', () => { if (!document.hidden) redraw(); });
function grids() { return Object.fromEntries(store.gauntlets.map((g) => [g.name, g.rows])); }
function draw(time) {
  const lv = cur();
  if (!lv) return;
  const gm = lv.game && lv.game.trace ? lv.game : null;
  const L = S.drag && S.drag.view ? S.drag.view : lv.L;
  const s = { L, zone: zoneOf(lv), grids: grids(), check: fresh(lv) ? lv.check : lv.check && lv.check.ok ? lv.check : null, game: gm, stale: !gameFresh(lv),
    overlays: S.overlays, hover: S.hover, preview: S.preview, selection: S.selection, changed: lv.changed, diff: lv.diff, time,
    selected: S.selected, hoverSpan: S.hoverSpan, hoverFinish: S.hoverFinish || (S.drag && S.drag.kind === 'finish'), replay: trace() ? replayPoint() : null,
    start: lv.start, hoverStart: S.hoverStart || (S.drag && S.drag.kind === 'start'), focus: S.focus };
  view.draw(s);
  view.minimap(s);
}

// ---- pointing at the course
const pointOf = (e) => { const r = canvas.getBoundingClientRect(); return { x: e.clientX - r.left, y: e.clientY - r.top }; };
const inCourse = (c) => { const lv = cur(); return lv && c >= 0 && c < lv.L.cols.length; };
function sawAt(p) { // a saw under the pointer: near its hub, round about its wall's top
  const lv = cur(), at = view.at(p.x, p.y);
  const i = K.sawNear(lv.L, at.x, Math.max(0.45, 8 / view.z * U));
  if (i < 0) return -1;
  const h = K.heightAt(lv.L, lv.L.saws[i]);
  return Math.abs(at.y / U - h) < 1.6 ? i : -1;
}
function discoAt(p) {
  const lv = cur();
  let best = -1, bd = Math.max(10, view.z * 0.7);
  lv.L.discos.forEach((d, i) => { const dx = view.X(d.x / U) - p.x, dy = view.Y(d.y / U) - p.y, dd = Math.hypot(dx, dy); if (dd < bd) { bd = dd; best = i; } });
  return best;
}
function spanUnder(p) { const lv = cur(), at = view.at(p.x, p.y); const s = K.spanAt(lv.L, at.c); if (!s) return null; const rows = gauntletNamed(s.name) ? gauntletNamed(s.name).rows.length : 12; return at.r < rows + 1 && at.r >= -1 ? s : null; }
function nearFinish(p) {
  const lv = cur(), h = view.finishHandle(lv.L.length);
  return Math.hypot(p.x - h.x, p.y - h.y) < 12 || (Math.abs(p.x - h.x) < 5 && p.y > h.top && p.y < view.oy);
}
function nearStart(p) { // (the start flag on the course: its handle, its pole)
  const lv = cur(), st = lv.start;
  if (!st || st.x === undefined) return false;
  const h = view.startHandle(st.x);
  return Math.hypot(p.x - h.x, p.y - h.y) < 12 || (Math.abs(p.x - h.x) < 5 && p.y > h.y && p.y < h.floor);
}
function onStartHen(lv, at) { // (the start in the maze, under the pointer: her two cells)
  const st = lv.start;
  if (!st || st.x !== undefined) return false;
  const sp = K.spanOf(lv.L, st.gauntlet);
  return !!sp && at.c === sp.c0 + st.c && (at.r === st.b || at.r === st.b + 1);
}
// the Start tool: on the course, a start there; in a gauntlet's span, the cell she'd stand in (on her: turned round)
function placeStart(lv, at, alt) {
  if (alt) { if (lv.start) { clearStart(lv); toast('Start point cleared: Play starts at the level’s beginning'); } return; }
  const sp = K.spanAt(lv.L, at.c);
  if (sp) {
    const g = gauntletNamed(sp.name);
    if (!g) return;
    if (onStartHen(lv, at)) return setStart(lv, { ...lv.start, dir: -lv.start.dir });
    const cell = G.standable(g.rows, at.c - sp.c0, G.index(g.rows, Math.max(0, at.r)));
    if (!cell) return toast('She can’t start there: on a floor (not a blade), with two cells of air for her body', 'bad');
    return setStart(lv, { gauntlet: sp.name, c: cell.c, b: cell.b, dir: 1 });
  }
  const x = K.startOnCourse(lv.L, at.x);
  if (x === null) return toast('Not there: on the course, short of the finish', 'bad');
  setStart(lv, { x });
  S.drag = { kind: 'start' };
}
function wallCols(L, c0, c1) { const out = []; for (let c = Math.max(0, Math.min(c0, c1)); c <= Math.min(L.cols.length - 1, Math.max(c0, c1)); c++) out.push(c); return out; }
function strokeCols(a, b) { const out = []; const s = a <= b ? 1 : -1; for (let c = a; ; c += s) { out.push(c); if (c === b) break; } return out; }

canvas.addEventListener('pointerdown', (e) => {
  const lv = cur();
  if (!lv) return;
  if (pickerOpen) togglePicker(false);
  const p = pointOf(e), at = view.at(p.x, p.y), right = e.button === 2, alt = e.altKey || right;
  canvas.setPointerCapture(e.pointerId);
  hideTip(); S.hazard = null;
  if (e.button === 1 || S.space) { S.drag = { kind: 'pan', x: p.x, y: p.y }; wrap.classList.add('panning'); return; }
  if (nearStart(p)) { if (alt) { clearStart(lv); toast('Start point cleared: Play starts at the level’s beginning'); } else S.drag = { kind: 'start' }; return; }
  if (nearFinish(p) && !alt) { S.drag = { kind: 'finish', snapshot: lv.L }; return; }
  switch (S.tool) {
    case 'start': return placeStart(lv, at, alt);
    case 'wall': {
      if (!inCourse(at.c) || K.spanAt(lv.L, at.c)) return;
      const h = alt ? 0 : Math.max(0, at.r + 1);
      S.drag = { kind: 'wall', snapshot: lv.L, last: at.c, lock: e.shiftKey ? h : null, zero: alt };
      return live(K.setHeight(lv.L, at.c, h, S.kind));
    }
    case 'paint': {
      if (!inCourse(at.c)) return;
      S.drag = { kind: 'paint', snapshot: lv.L, last: at.c };
      return live(K.setKind(lv.L, at.c, S.kind));
    }
    case 'ceil': {
      if (!inCourse(at.c)) return;
      S.drag = { kind: 'ceil', c0: at.c, r: Math.max(2, at.r), remove: alt };
      S.preview = ceilPreview(at.c, at.c, Math.max(2, at.r), alt);
      return redraw();
    }
    case 'saw': {
      const i = sawAt(p);
      if (i >= 0) {
        if (alt) { S.selected = null; return commit(K.removeSaw(lv.L, i), 'saw-remove'); }
        S.selected = { saw: i }; S.drag = { kind: 'saw', snapshot: lv.L, i, moved: false };
        return redraw();
      }
      if (alt) return;
      const x = K.snapSaw(lv.L, at.x);
      const next = K.addSaw(lv.L, x);
      if (next === lv.L) return toast(K.inGauntlet(lv.L, x) ? 'A gauntlet’s blades are its grid’s (edit its grid)' : 'Not on top of another blade', 'bad');
      commit(next, 'saw-add');
      S.selected = { saw: cur().L.saws.indexOf(Math.round(x * 1000) / 1000) };
      return redraw();
    }
    case 'disco': {
      const i = discoAt(p);
      if (i >= 0) {
        if (alt) { S.selected = null; return commit(K.removeDisco(lv.L, i), 'disco-remove'); }
        S.selected = { disco: i }; S.drag = { kind: 'disco', snapshot: lv.L, i, moved: false };
        return redraw();
      }
      if (alt) return;
      const x = Math.round(at.x * 10) / 10, y = Math.round(Math.max(0.3, at.y) * 20) / 20;
      commit(K.addDisco(lv.L, x, y), 'disco-add');
      S.selected = { disco: cur().L.discos.findIndex((d) => d.x === x && d.y === y) };
      return redraw();
    }
    case 'cols': {
      if (!inCourse(at.c)) return;
      S.drag = { kind: 'cols', remove: alt };
      S.preview = { kind: 'cols', c0: at.c, c1: at.c, remove: alt };
      return redraw();
    }
    case 'select': {
      if (!inCourse(at.c)) return;
      S.drag = { kind: 'select' };
      S.selection = { c0: at.c, c1: at.c };
      return redraw();
    }
    case 'gauntlet': {
      const s = spanUnder(p) || K.spans(lv.L)[0];
      if (!s) return toast(`Level ${lv.number} has no gauntlet: pick one in the inspector (The level → Gauntlet)`, 'bad');
      const info = gridInfo(s.name);
      if (!info) return toast(`The ${titled(s.name)}’s grid needs both doors`, 'bad');
      S.drag = { kind: 'gauntlet', name: s.name, info, grab: spanUnder(p) ? at.c - s.c0 : Math.floor(info.cols / 2), c: s.c0 };
      S.preview = gauntletPreview(s.name, info, s.c0);
      return redraw();
    }
    default: break;
  }
});
function ceilPreview(c0, c1, r, remove) { const L = cur().L; return { kind: 'ceil', c0, c1, r, remove, ok: (c) => !!L.cols[c] && !L.cols[c].g && r >= L.cols[c].h + 2 }; }
function gauntletPreview(name, info, c) {
  const L = cur().L, fp = K.gauntletFootprint(info, Math.max(K.FORECOURT + K.LINTEL, c));
  return { kind: 'gauntlet', a: fp.a, c: fp.c, z: fp.z, grid: info, ok: K.placeGauntlet(L, name, info, c) !== L || (K.spanOf(L, name) && K.spanOf(L, name).c0 === c) };
}
canvas.addEventListener('pointermove', (e) => {
  const lv = cur();
  if (!lv) return;
  const p = pointOf(e), d = S.drag, at = view.at(p.x, p.y);
  if (d && d.kind === 'pan') { view.panBy(p.x - d.x, p.y - d.y); d.x = p.x; d.y = p.y; return; }
  const moved = !S.hover || S.hover.c !== at.c || S.hover.r !== at.r;
  S.hover = { c: at.c, r: at.r, x: at.x, y: at.y };
  S.hoverFinish = !d && nearFinish(p);
  S.hoverStart = !d && (nearStart(p) || onStartHen(lv, at));
  const sp = !d ? spanUnder(p) : null;
  S.hoverSpan = sp ? sp.name : null;
  updateCursor(p);
  if (moved) renderHover();
  if (!d) { hoverPreview(p, at, e); hazardCard(e, p, at); return; }
  if (d.kind === 'start') { const x = K.startOnCourse(lv.L, at.x); if (x !== null && (!lv.start || x !== lv.start.x)) setStart(lv, { x }); return; }
  if (d.kind === 'finish') { live(K.setLength(lv.L, Math.round(Math.max(K.MIN_LENGTH, at.x)))); renderProps(lv, true); return; }
  if (!moved && d.kind !== 'saw' && d.kind !== 'disco') return;
  if (d.kind === 'wall' || d.kind === 'paint') {
    let L = lv.L;
    for (const c of strokeCols(d.last, at.c)) {
      if (!inCourse(c)) continue;
      L = d.kind === 'paint' ? K.setKind(L, c, S.kind) : K.setHeight(L, c, d.zero ? 0 : d.lock ?? Math.max(0, at.r + 1), S.kind);
    }
    d.last = at.c;
    live(L);
  } else if (d.kind === 'ceil') S.preview = ceilPreview(d.c0, at.c, d.r, d.remove);
  else if (d.kind === 'cols') S.preview.c1 = at.c;
  else if (d.kind === 'select') S.selection.c1 = at.c;
  else if (d.kind === 'saw') {
    const x = K.snapSaw(lv.L, at.x, d.i), next = K.moveSaw(d.moved ? lv.L : d.snapshot, d.moved ? S.selected.saw : d.i, x);
    if (next !== lv.L && next !== d.snapshot) { d.moved = true; live(next); S.selected = { saw: next.saws.indexOf(Math.round(x * 1000) / 1000) }; }
  } else if (d.kind === 'disco') {
    const x = Math.round(at.x * 10) / 10, y = Math.round(Math.max(0.3, at.y) * 20) / 20;
    const next = K.moveDisco(lv.L, S.selected.disco, x, y);
    if (next !== lv.L) { d.moved = true; live(next); S.selected = { disco: next.discos.findIndex((q) => q.x === x && q.y === y) }; }
  } else if (d.kind === 'gauntlet') {
    d.c = Math.max(K.FORECOURT + K.LINTEL, at.c - d.grab);
    S.preview = gauntletPreview(d.name, d.info, d.c);
  }
});
function hoverPreview(p, at, e) {
  const lv = cur();
  const alt = e.altKey;
  if (S.tool === 'wall' && inCourse(at.c) && !K.spanAt(lv.L, at.c)) S.preview = { kind: 'wall', cols: [{ c: at.c, h: alt ? 0 : Math.max(0, at.r + 1), kind: lv.L.cols[at.c].h ? lv.L.cols[at.c].kind : S.kind }] };
  else if (S.tool === 'ceil' && inCourse(at.c)) S.preview = ceilPreview(at.c, at.c, Math.max(2, at.r), alt);
  else if (S.tool === 'saw' && sawAt(p) < 0 && inCourse(at.c)) { const x = K.snapSaw(lv.L, at.x), k = lv.L.cols[K.colAt(x)]; S.preview = k ? { kind: 'saw', x, h: k.g ? 0 : k.h, cube: !k.g && k.h > 0 && k.kind !== 'crate', ok: K.addSaw(lv.L, x) !== lv.L } : null; }
  else if (S.tool === 'disco' && discoAt(p) < 0) S.preview = { kind: 'disco', x: Math.round(at.x * 10) / 10, y: Math.round(Math.max(0.3, at.y) * 20) / 20 };
  else if (S.tool === 'cols' && inCourse(at.c)) S.preview = { kind: 'cols', c0: at.c, c1: at.c, remove: alt };
  else if (S.tool === 'select' && S.clipboard && inCourse(at.c)) S.preview = { kind: 'insert', c: at.c };
  else S.preview = null;
}
function updateCursor(p) {
  const d = S.drag;
  let cur = S.space ? 'grab' : 'crosshair';
  if (d && d.kind === 'pan') cur = 'grabbing';
  else if (d && (d.kind === 'finish' || d.kind === 'gauntlet')) cur = 'ew-resize';
  else if (S.hoverFinish || (S.hoverStart && cur !== 'grab')) cur = 'ew-resize';
  else if (d && d.kind === 'start') cur = 'ew-resize';
  else if (p && S.tool === 'saw' && sawAt(p) >= 0) cur = 'grab';
  else if (p && S.tool === 'disco' && discoAt(p) >= 0) cur = 'grab';
  else if (S.tool === 'gauntlet' && S.hoverSpan) cur = 'grab';
  canvas.style.cursor = cur;
}
function endDrag() {
  const lv = cur(), d = S.drag;
  S.drag = null;
  wrap.classList.remove('panning');
  if (!lv || !d) return;
  const kind = { wall: 'walls', paint: 'kind', finish: 'finish', saw: 'saw-move', disco: 'disco-move' }[d.kind];
  if (kind) {
    if (lv.L !== d.snapshot && d.snapshot) { const L = lv.L; lv.L = d.snapshot; commit(L, kind, { snapshot: d.snapshot }); }
  } else if (d.kind === 'ceil') {
    const p = S.preview; S.preview = null;
    let L = lv.L;
    for (const c of wallCols(L, p.c0, p.c1)) if (!L.cols[c].g) L = p.remove ? K.setCeil(L, c, 0) : (p.ok(c) ? K.setCeil(L, c, p.r) : L);
    if (L === lv.L && !p.remove) toast('A barrier goes 2 eggs over a wall’s top at least (her body under it)', 'bad');
    commit(L, p.remove ? 'ceil-off' : 'ceil');
  } else if (d.kind === 'cols') {
    const p = S.preview; S.preview = null; S.selection = null;
    const a = Math.min(p.c0, p.c1), b = Math.max(p.c0, p.c1);
    const L = p.remove ? K.deleteCols(lv.L, a, b) : K.duplicateCols(lv.L, a, b);
    if (L === lv.L) toast(p.remove ? 'Not a gauntlet’s columns (move it, or take it out), and the course keeps 30 m' : 'Not inside a gauntlet (its grid’s columns are the grid’s: edit its grid)', 'bad');
    commit(L, p.remove ? 'cols-out' : 'cols-in', { hint: p.remove ? { c0: a, c1: b } : { c0: b + 1, c1: b + 1 + (b - a) } });
  } else if (d.kind === 'gauntlet') {
    S.preview = null;
    const s = K.spanOf(lv.L, d.name);
    if (s && s.c0 === d.c) return redraw();
    const L = K.placeGauntlet(lv.L, d.name, d.info, d.c);
    if (L === lv.L) toast('No room for it there', 'bad');
    else { commit(L, 'gauntlet'); toast(`The ${titled(d.name)} moved to ${Math.round(d.c * U)} m, its forecourt and porch with it`, 'ok'); }
  }
  renderProps(lv);
  redraw();
}
canvas.addEventListener('pointerup', endDrag);
canvas.addEventListener('pointercancel', endDrag);
canvas.addEventListener('pointerleave', () => { if (!S.drag) { S.hover = null; S.preview = null; S.hoverSpan = null; S.hoverFinish = false; S.hoverStart = false; renderHover(); } clearTimeout(hazardTimer); S.hazard = null; hideTip(); });
canvas.addEventListener('contextmenu', (e) => e.preventDefault());
canvas.addEventListener('dblclick', (e) => { const sp = cur() && spanUnder(pointOf(e)); if (sp) emit('show-gauntlet', sp.name); });
canvas.addEventListener('wheel', (e) => {
  e.preventDefault();
  const p = pointOf(e);
  if (e.ctrlKey || e.metaKey) view.zoomAt(p.x, p.y, Math.exp(-e.deltaY * (e.ctrlKey && !e.metaKey ? 0.01 : 0.0025)));
  else if (e.shiftKey && !e.deltaX) view.panBy(-e.deltaY, 0);
  else view.panBy(-e.deltaX, -e.deltaY);
}, { passive: false });
// What's under the pointer, for its card (tips.js, and its particulars): the start, the finish, a saw, a disco ball, a
// barrier, the gauntlet, a note, a wall
function hazardAt(lv, p, at) {
  const t = (key, extra) => ({ id: key + ':' + extra, title: TIPS[key].title, text: TIPS[key].text, extra });
  if (S.hoverStart) return t('hazard.start', lv.start.x !== undefined ? `At ${lv.start.x} m.` : `In the ${titled(lv.start.gauntlet)}, col ${lv.start.c}, row ${lv.start.b}, running ${lv.start.dir < 0 ? 'left' : 'right'}. Click her (Start tool) to turn her round.`);
  if (S.hoverFinish) return t('hazard.finish', `At ${Math.round(lv.L.length)} m.`);
  const si = sawAt(p);
  if (si >= 0) {
    const x = lv.L.saws[si], k = lv.L.cols[K.colAt(x)], cube = k && !k.g && k.h > 0 && k.kind !== 'crate';
    const row = lv.L.saws.filter((y) => Math.abs(Math.abs(y - x) - K.SAW_ROW_GAP) < 0.05).length;
    return t(cube ? 'hazard.cube-saw' : 'hazard.saw', `At ${x} m, in ${k && k.h ? `a wall ${k.h} high` : 'the street'}${row ? `; ${row === 1 ? 'a blade' : 'blades'} back to back with it` : ''}. ${cube ? TIPS['hazard.saw'].text : ''}`.trim());
  }
  const di = discoAt(p);
  if (di >= 0) { const d = lv.L.discos[di]; return t('hazard.disco', `At ${d.x} m, ${d.y} m up.`); }
  const k = inCourse(at.c) ? lv.L.cols[at.c] : null;
  if (k && !k.g && k.ceil && at.r >= k.ceil - 1 && at.r <= k.ceil + 2) {
    const fit = Math.max(0, Math.floor((k.ceil * U - 1.05 - 0.02) / U + 1e-6) - k.h);
    return t('hazard.barrier', `Its underside at ${k.ceil}, over ${k.h ? `a wall ${k.h} high` : 'the street'}: ${fit} egg${fit === 1 ? '' : 's'} fit under it there.`);
  }
  if (S.hoverSpan) { const g = gauntletNamed(S.hoverSpan); return t('hazard.gauntlet', `The ${titled(S.hoverSpan)}${g ? `, ${G.cols(g.rows)} × ${g.rows.length}` : ''}.`); }
  const ch = lv.check && lv.check.ok ? lv.check : null;
  if (ch && S.overlays.notes) {
    const n = [...(ch.doors || []), ...ch.short, ...ch.notes].find((q) => Math.abs(view.X(q.x / U) - p.x) < 9 && Math.abs(view.Y(q.y) - 12 - p.y) < 10);
    if (n) return t('hazard.note', n.text);
  }
  if (k && !k.g && k.h > 0 && at.r >= 0 && at.r < k.h) return t('hazard.wall', `${titled(k.kind === 'hay' ? 'hay' : k.kind === 'mix' ? 'hay-topped crates' : 'crates')}, ${k.h} high.`);
  return null;
}
let hazardTimer = 0;
function hazardCard(e, p, at) {
  const lv = cur(), hz = hazardAt(lv, p, at);
  if (!hz) { clearTimeout(hazardTimer); if (S.hazard) { S.hazard = null; hideTip(); } return; }
  if (S.hazard && S.hazard.id === hz.id) { showAt(e.clientX, e.clientY, { title: hz.title, text: `${hz.text} ${hz.extra}`.trim() }); return; } // (follows the pointer)
  clearTimeout(hazardTimer);
  S.hazard = null; hideTip();
  const x = e.clientX, y = e.clientY;
  hazardTimer = setTimeout(() => { if (S.drag) return; S.hazard = hz; showAt(x, y, { title: hz.title, text: `${hz.text} ${hz.extra}`.trim() }); }, 420);
}
function renderHover() {
  const lv = cur(), h = S.hover, out = $('lhoverinfo');
  if (!lv) return;
  if (!h || !inCourse(h.c)) { out.innerHTML = `${Math.round(lv.L.length)} m to the finish · ${lv.L.cols.length} columns · a cell is 0.6 m`; return; }
  const k = lv.L.cols[h.c];
  let what;
  if (k.g) {
    const s = K.spanAt(lv.L, h.c);
    what = `the <b>${esc(titled(k.g))}</b>, its col ${h.c - s.c0}`;
  } else what = k.h ? `${k.kind === 'hay' ? 'hay' : k.kind === 'mix' ? 'hay-topped crates' : 'crates'} <b>${k.h}</b> high` : 'the street';
  if (k.ceil) what += ` · a barrier at <b>${k.ceil}</b>`;
  const saw = K.sawNear(lv.L, h.x, 0.6);
  if (saw >= 0) what += ` · a saw at ${lv.L.saws[saw]} m`;
  out.innerHTML = `<b>${h.x.toFixed(1)} m</b> · col ${h.c} · row ${Math.max(0, h.r)} · ${what}`;
}

// the minimap: click or drag to look along the course
const mini = $('lminimap');
let miniDrag = false;
function miniLook(e) { const r = mini.getBoundingClientRect(), x = view.miniToX(e.clientX - r.left); if (x !== null) view.centerOnX(x); }
mini.addEventListener('pointerdown', (e) => { miniDrag = true; mini.setPointerCapture(e.pointerId); miniLook(e); });
mini.addEventListener('pointermove', (e) => { if (miniDrag) miniLook(e); });
mini.addEventListener('pointerup', () => { miniDrag = false; });
const ltrack = $('ltrack');
let trackDrag = false;
ltrack.addEventListener('pointerdown', (e) => { trackDrag = true; ltrack.setPointerCapture(e.pointerId); trackSeek(e); });
ltrack.addEventListener('pointermove', (e) => { if (trackDrag) trackSeek(e); });
ltrack.addEventListener('pointerup', () => { trackDrag = false; });

// ---- selections of columns: copy, cut, paste, take out
function copySel(cut = false) {
  const lv = cur(), s = S.selection;
  if (!lv || !s) return;
  const clip = K.extractCols(lv.L, s.c0, s.c1);
  if (!clip) return toast('Not a gauntlet’s columns: its grid’s are the grid’s', 'bad');
  S.clipboard = clip;
  if (cut) { commit(K.deleteCols(lv.L, s.c0, s.c1), 'cut', { hint: { c0: Math.min(s.c0, s.c1), c1: Math.max(s.c0, s.c1) } }); S.selection = null; }
  toast(`${cut ? 'Cut' : 'Copied'} ${plural(clip.cols.length, 'column')}: ⌘V puts them in where you point`);
}
function pasteCols() {
  const lv = cur();
  if (!lv || !S.clipboard) return;
  const c = S.hover && inCourse(S.hover.c) ? S.hover.c : S.selection ? Math.max(S.selection.c0, S.selection.c1) + 1 : 0;
  const L = K.insertCols(lv.L, c, S.clipboard);
  if (L === lv.L) return toast('Not inside a gauntlet', 'bad');
  commit(L, 'paste', { hint: { c0: c, c1: c + S.clipboard.cols.length - 1 } });
  S.selection = { c0: c, c1: c + S.clipboard.cols.length - 1 };
}
function deleteSel() { const lv = cur(), s = S.selection; if (lv && s) { const L = K.deleteCols(lv.L, s.c0, s.c1); if (L === lv.L) toast('Not a gauntlet’s columns, and the course keeps 30 m', 'bad'); commit(L, 'cols-out', { hint: { c0: Math.min(s.c0, s.c1), c1: Math.max(s.c0, s.c1) } }); S.selection = null; } }

// ---- the keyboard: true if it took the key
export function keydown(e) {
  const mod = e.metaKey || e.ctrlKey, k = e.key.toLowerCase();
  if (mod && k === 'c') { copySel(); return true; }
  if (mod && k === 'x') { e.preventDefault(); copySel(true); return true; }
  if (mod && k === 'v') { e.preventDefault(); pasteCols(); return true; }
  if (mod) return false;
  if (e.altKey && (e.key === 'ArrowLeft' || e.key === 'ArrowRight')) { const n = cur().number + (e.key === 'ArrowLeft' ? -1 : 1); if (levelNumbered(n)) emit('show-level', n); return true; }
  if (e.key === ' ') { e.preventDefault(); if (!S.space) { S.space = true; updateCursor(); } return true; }
  const kind = KINDS.find((x) => x.key === e.key);
  if (kind) { S.kind = kind.id; renderTools(); return true; }
  const tool = TOOLS.find((t) => t.key === k);
  if (tool) { setTool(tool.id); return true; }
  switch (e.key) {
    case 'l': case 'L': togglePicker(); return true;
    case 'f': case 'F': view.fit(cur().L, grids(), S.hover ? S.hover.x : null); return true;
    case '[': view.zoomAt(view.w / 2, view.h / 2, 1 / 1.25); return true;
    case ']': view.zoomAt(view.w / 2, view.h / 2, 1.25); return true;
    case 'p': case 'P': playPause(); return true;
    case ',': R.playing = false; renderTransport(); setReplay(R.i - 12); return true;
    case '.': R.playing = false; renderTransport(); setReplay(R.i + 12); return true;
    case 'Home': setReplay(0); return true;
    case 'End': { const tr = trace(); if (tr) setReplay(tr.length - 1); return true; }
    case 'Escape': if (pickerOpen) togglePicker(false); S.selection = null; S.preview = null; S.selected = null; return true;
    case 'Delete': case 'Backspace': {
      e.preventDefault();
      if (S.selected && S.selected.saw >= 0) { commit(K.removeSaw(cur().L, S.selected.saw), 'saw-remove'); S.selected = null; }
      else if (S.selected && S.selected.disco >= 0) { commit(K.removeDisco(cur().L, S.selected.disco), 'disco-remove'); S.selected = null; }
      else if (S.selection) deleteSel();
      return true;
    }
    default: return false;
  }
}
export function keyup(e) { if (e.key === ' ') { S.space = false; updateCursor(); } }

// ---- playing it in the desktop game, as edited
export async function play(bot) {
  const lv = cur();
  if (!lv) return;
  const start = startRequest(lv), where = start ? startLabel(lv) : 'from its start';
  try {
    await api('POST', '/api/play', { level: lv.number, levelText: lv.text, grids: unsavedGrids(), bot, atGauntlet: false, start });
    toast(bot ? `The desktop game is opening: the bot plays level ${lv.number} ${where}, every run.` : `The desktop game is opening on level ${lv.number}, every run ${where}: tap (or Space) to start, Space lays an egg.`, 'ok');
  } catch (e) { toast(e.message, 'bad'); }
}

// ---- switching levels, and the view in and out
export function select(n) {
  const lv = levelNumbered(n);
  if (!lv) return false;
  if (S.cur && S.cur !== lv) S.cur.view = { z: view.z, ox: view.ox, oy: view.oy, i: R.i };
  const fit = S.cur !== lv;
  S.cur = lv;
  S.selection = null; S.preview = null; S.selected = null; R.playing = false;
  local.set('cuckoo-level-current', n);
  if (fit) {
    view.resize();
    if (lv.view) { view.z = lv.view.z; view.ox = lv.view.ox; view.oy = lv.view.oy; R.i = lv.view.i; view.measure(lv.L, grids()); } else { view.fit(lv.L, grids()); R.i = 0; }
  }
  if (!fresh(lv)) scheduleCheck(lv, 0);
  if (!gameFresh(lv) && !lv.gameBusy) scheduleGame(lv, 60);
  renderTools(); renderNav(); renderPanels(); renderTransport(); renderTrack(); renderReadout(); renderHover(); renderPicker(); redraw();
  return true;
}
export function enter() {
  S.active = true;
  view.resize();
  if (S.cur) view.measure(S.cur.L, grids());
  renderTools(); renderChips(); renderNav(); renderPanels(); renderTransport(); renderTrack(); renderReadout(); renderHover(); redraw();
}
export function leave() { S.active = false; R.playing = false; S.hover = null; togglePicker(false); }
export function resize() { if (!S.active) return; view.resize(); renderTrack(); }
export function init(kept = new Map()) { // (kept: the histories this browser kept, history.js)
  for (const lv of store.levels) {
    lv.L = fitted(lv.L); lv.text = K.levelText(lv.L, lv.number);
    if (!restoreHistory(lv, kept.get('level:' + lv.number)) && H.current(lv.hist) !== lv.text) lv.hist = H.record(lv.hist, lv.text, 'Refitted to its gauntlet’s grid', null, 'refit');
    diffSaved(lv);
    checkStart(lv);
  }
}
// checks for every level, a few at a time (the picker's dots; the bot's runs only as they're opened)
export function checkAll() { store.levels.forEach((lv, i) => scheduleCheck(lv, 200 + i * 120)); }

$('lzoomIn').onclick = () => view.zoomAt(view.w / 2, view.h / 2, 1.25);
$('lzoomOut').onclick = () => view.zoomAt(view.w / 2, view.h / 2, 1 / 1.25);
$('lzoomFit').onclick = () => { if (cur()) view.fit(cur().L, grids()); };
$('lrpPlay').onclick = playPause;
$('lrpFollow').onclick = () => { R.follow = !R.follow; renderTransport(); };
$('lrpStart').onclick = () => { R.playing = false; renderTransport(); setReplay(0); };
$('ldescription').addEventListener('focus', () => { S.notesBefore = cur() ? cur().L : null; });
$('ldescription').addEventListener('input', (e) => {
  const lv = cur();
  if (!lv) return;
  lv.L = { ...lv.L, notes: e.target.value.split('\n') };
  lv.text = K.levelText(lv.L, lv.number);
  storeDraft(lv); renderNav(); emit('top');
});
$('ldescription').addEventListener('change', () => { // (left: one edit on the history)
  const lv = cur();
  if (!lv) return;
  const L = lv.L;
  if (S.notesBefore) { lv.L = S.notesBefore; commitTo(lv, L, 'notes'); } else refresh(lv);
  S.notesBefore = lv.L;
});

export const debug = { view, state: S, replay: R, course: K, select, commit, setStart };
