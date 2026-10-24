// The level editor (tools/level_editor/server.py serves it): the campaign's levels and their maze gauntlets, tuned with
// the game's own checkers judging every edit and the real game's bot playing them a moment after. Two views, one
// shell: Levels (levels.js: a level's whole course, which gauntlet it plays and where) and Gauntlets (gauntlets.js: a
// gauntlet's grid). Save saves every edit together (a gauntlet moved between levels changes both) into the game's
// files; Play plays the level as edited in the desktop game; Simulator puts what's saved on the iOS Simulator.
import * as K from './course.js';
import * as Gm from './gauntlets.js';
import * as Lv from './levels.js';
import { $, el, esc, titled, api, toast, local, list } from './util.js';
import { modal, ask } from './dialog.js';
import * as H from './history.js';
import { setTip } from './tips.js';
import { initTooltips } from './tooltip.js';
import { store, on, levelNumbered, gauntletNamed, levelDirty, gauntletDirty, gauntletOfLevel } from './store.js';

let mode = null;
const views = { level: Lv, gauntlet: Gm };
const active = () => views[mode || 'level'];
Lv.setGauntletVerdict((g) => { Gm.ensureFresh(g); return Gm.verdict(g); });

// ---- the two views
function setMode(m) {
  if (m === mode) return;
  if (mode) active().leave();
  mode = m;
  document.body.dataset.mode = m;
  local.set('cuckoo-editor-mode', m);
  active().enter();
  renderTop();
}
function showLevel(n) {
  if (!levelNumbered(n)) return;
  setMode('level');
  Lv.select(n);
  history.replaceState(null, '', `#level/${n}`);
  renderTop();
}
function showGauntlet(name) {
  if (!gauntletNamed(name)) return;
  setMode('gauntlet');
  Gm.select(name);
  history.replaceState(null, '', `#gauntlet/${name}`);
  renderTop();
}
on('show-level', showLevel);
on('show-gauntlet', showGauntlet);
on('new-gauntlet', async (n) => {
  const lv = levelNumbered(n);
  const g = await Gm.newGauntlet();
  if (!g || !lv) return;
  if (!gauntletOfLevel(lv)) { Lv.adoptGauntlet(n, g.name); toast(`The ${titled(g.name)} is in level ${n} now, at 40% of the way: design its grid here, then see it in the level`, 'ok'); }
});
on('top', () => renderTop());
on('start', () => renderTop());
on('level', (lv) => { const g = gauntletOfLevel(lv); if (g) Gm.levelChanged(g.name); });
on('gauntlet-checked', () => { if (mode === 'level') { Lv.renderPanels(); Lv.renderNav(); } });

// ---- the top bar
function renderTop() {
  const v = active(), c = v.current();
  const dl = store.levels.filter(levelDirty), dg = store.gauntlets.filter(gauntletDirty);
  const names = [...dl.map((l) => `level ${l.number}`), ...dg.map((g) => `the ${g.name}`)];
  const d = $('dirty');
  d.hidden = !names.length;
  d.textContent = `${names.length} unsaved`;
  setTip(d, 'top.unsaved', names.length ? `Unsaved: ${list(names)}.` : '');
  $('undo').disabled = !v.canUndo();
  $('redo').disabled = !v.canRedo();
  setTip($('undo'), 'top.undo', v.canUndo() ? `Next: “${v.undoLabel()}”.` : 'Nothing to undo.');
  setTip($('redo'), 'top.redo', v.canRedo() ? `Next: “${v.redoLabel()}”.` : 'Nothing to redo.');
  // where Play starts: this level's start point (in the Gauntlets view, one in this maze)
  const lv = mode === 'level' ? Lv.current() : c && store.levels.find((l) => l.L.cols.some((k) => k.g === c.name));
  const st = lv && Lv.startOf(lv), shown = st && (mode === 'level' || st.gauntlet === (c && c.name));
  for (const id of ['lstartChip', 'gstartChip']) { // (on the canvas, in its corner)
    const chip = $(id);
    chip.hidden = !shown || id[0] !== (mode === 'level' ? 'l' : 'g');
    if (chip.hidden) continue;
    chip.querySelector('.where').textContent = Lv.startLabel(lv);
    chip.querySelector('.x').onclick = (e) => { e.stopPropagation(); Lv.clearStart(lv); toast(mode === 'level' ? 'Start point cleared: Play starts at the level’s beginning' : 'Start point cleared: Play starts at its door'); };
  }
  $('revert').disabled = !names.length;
  setTip($('revert'), 'top.revert', names.length ? '' : 'Nothing unsaved to revert.');
  $('save').disabled = !names.length;
  void c;
  for (const b of $('modes').querySelectorAll('button')) b.setAttribute('aria-selected', String(b.dataset.mode === mode));
  const fromDoor = mode === 'gauntlet' && !shown ? 'Here: every run from the gauntlet’s door.' : '';
  setTip($('play'), 'top.play', fromDoor);
  setTip($('watch'), 'top.watch', fromDoor);
}
for (const b of $('modes').querySelectorAll('button')) {
  b.onclick = () => {
    if (b.dataset.mode === 'level') { const g = Gm.current(), lv = g && store.levels.find((l) => l.L.cols.some((k) => k.g === g.name)); showLevel(lv ? lv.number : (Lv.current() || store.levels[0]).number); }
    else { const lv = Lv.current(), g = lv && gauntletOfLevel(lv); showGauntlet(g ? g.name : (Gm.current() || store.gauntlets[0]).name); }
  };
}

// ---- the unsaved edits: reverted (those ticked: what's on screen, else every one), or saved together
function unsavedItems() {
  return [
    ...store.levels.filter(levelDirty).map((lv) => ({ kind: 'level', ref: lv, name: `Level ${lv.number}`, what: 'its course', sub: Lv.changeSummary(lv) || 'edited', edits: H.sinceSaved(lv.hist), open: () => showLevel(lv.number) })),
    ...store.gauntlets.filter(gauntletDirty).map((g) => ({ kind: 'gauntlet', ref: g, name: `The ${titled(g.name)}`, what: 'its grid', sub: Gm.changeSummary(g) || 'edited', edits: g.saved ? H.sinceSaved(g.hist) : [], open: () => showGauntlet(g.name) })),
  ];
}
function onScreen(it) { // (the level on screen and the gauntlet it lays; the grid on screen)
  if (mode === 'level') { const lv = Lv.current(); return !!lv && (it.ref === lv || (it.kind === 'gauntlet' && gauntletOfLevel(lv) === it.ref)); }
  return it.ref === Gm.current();
}
async function unsaved(from = 'revert') {
  const items = unsavedItems();
  if (!items.length) return;
  const here = items.some(onScreen);
  const body = el('div');
  body.append(el('p', null, 'Revert takes the ticked ones back to what’s saved: Undo brings their edits back (a gauntlet never saved goes for good). Save saves every one, together.'));
  const ul = el('ul', 'unsaved-list');
  const ticks = items.map((it) => {
    const li = el('li', it.kind);
    const box = el('input');
    box.type = 'checkbox';
    box.checked = here ? onScreen(it) : true;
    const label = el('label', null, `<b>${esc(it.name)}</b><span>${esc(it.what)}: ${esc(it.sub)}</span>`);
    label.prepend(box);
    const open = el('button', 'small ghost', 'Show');
    open.dataset.tip = `Opens ${it.name.toLowerCase()} with its changes since the save shown (the Changes overlay).`;
    open.onclick = () => { closeIt(null); it.open(); };
    li.append(label, open);
    if (it.edits.length) { // (what's in it: its edits since the save, newest first)
      const more = el('details', 'h-edits');
      more.append(el('summary', null, `${it.edits.length} edit${it.edits.length === 1 ? '' : 's'} since the save`));
      const ol = el('ol');
      for (const e of it.edits.slice(0, 40)) ol.append(el('li', null, `<span>${esc(e.label)}</span><i>${esc(H.ago(e.at))}</i>`));
      if (it.edits.length > 40) ol.append(el('li', 'more', `…and ${it.edits.length - 40} before`));
      more.append(ol);
      li.append(more);
    }
    ul.append(li);
    return box;
  });
  body.append(ul);
  let closeIt = () => {};
  const v = await modal({ title: items.length === 1 ? '1 unsaved edit' : `${items.length} unsaved edits`, body, wide: true,
    actions: [{ label: 'Cancel' }, { label: 'Revert', value: 'revert', kind: 'danger', default: from === 'revert' }, { label: 'Save all', value: 'save', kind: 'primary', default: from !== 'revert' }],
    onOpen: ({ buttons, close }) => {
      closeIt = close;
      const update = () => { const n = ticks.filter((b) => b.checked).length; buttons[1].textContent = n ? `Revert ${n === items.length && n > 1 ? 'all ' : ''}${n}` : 'Revert'; buttons[1].disabled = !n; };
      for (const b of ticks) b.addEventListener('change', update);
      update();
    } });
  if (v === 'save') return save();
  if (v !== 'revert') return;
  const picked = items.filter((_, i) => ticks[i].checked);
  for (const it of picked) if (it.kind === 'gauntlet') Gm.revertGauntlet(it.ref); // (grids first: a level refits to its grid)
  for (const it of picked) if (it.kind === 'level') Lv.revertLevel(it.ref);
  if (mode === 'gauntlet' && !Gm.current()) { const lv = store.levels.find((l) => picked.some((it) => it.kind === 'gauntlet' && l.saved.text.includes(` gauntlet ${it.ref.name}\n`))); if (lv) showLevel(lv.number); else if (store.gauntlets[0]) showGauntlet(store.gauntlets[0].name); }
  Gm.renderTabs(); Gm.renderPanels(); Lv.renderNav(); Lv.renderPanels(); Lv.renderPicker();
  renderTop();
  const lower = (it) => it.name.replace(/^(The|Level) /, (m) => m.toLowerCase());
  const gone = picked.filter((it) => it.kind === 'gauntlet' && !it.ref.saved).map(lower);
  const back = picked.filter((it) => !(it.kind === 'gauntlet' && !it.ref.saved)).map(lower);
  toast([back.length ? `Back to what’s saved: ${list(back)} (Undo brings the edits back)` : '', gone.length ? `${list(gone)}: thrown away` : ''].filter(Boolean).join('; '), 'ok');
}

// ---- saving everything changed, together
async function save() {
  const levels = store.levels.filter(levelDirty), grids = store.gauntlets.filter(gauntletDirty);
  if (!levels.length && !grids.length) return;
  const failing = [];
  for (const lv of levels) { const v = Lv.verdict(lv, Gm.verdict); if (v.state === 'bad') failing.push(`Level ${lv.number}: ${v.items.filter((it) => it.ok === false).map((it) => it.what.toLowerCase()).join('; ')}`); }
  for (const g of grids) { const v = Gm.verdict(g); if (v.state === 'bad') failing.push(`The ${titled(g.name)}: ${v.items.filter((it) => it.ok === false).map((it) => it.what.toLowerCase()).join('; ')}`); }
  if (failing.length && !(await ask({ title: 'Save it anyway?', ok: 'Save anyway', danger: true,
    body: `<p>Not everything passes its checks yet:</p><ul class="failing">${failing.map((f) => `<li>${esc(f)}</li>`).join('')}</ul><p>The tests will fail, and block commits, until they pass.</p>` }))) return;
  let res;
  try {
    res = await api('POST', '/api/save', { levels: Object.fromEntries(levels.map((lv) => [lv.number, lv.text])),
      gauntlets: Object.fromEntries(grids.map((g) => [g.name, { rows: g.rows, description: g.description }])) });
  } catch (e) { return toast(e.message, 'bad'); }
  for (const lv of levels) Lv.saved(lv);
  for (const g of grids) Gm.saved(g);
  for (const [n, text] of Object.entries(res.refit || {})) { // (a level the server refitted to a grid it saved)
    const lv = levelNumbered(Number(n));
    if (lv && !levelDirty(lv)) { lv.L = K.parseLevel(text); lv.text = text; Lv.saved(lv); }
  }
  const what = [...levels.map((lv) => `level ${lv.number}`), ...grids.map((g) => `the ${g.name}`)];
  toast(`Saved ${list(what)} (${res.wrote.length} file${res.wrote.length === 1 ? '' : 's'}, and ${list(res.incs)}). Rebuild the game to ship ${what.length === 1 ? 'it' : 'them'}; Play plays ${what.length === 1 ? 'it' : 'them'} now.`, 'ok');
  Gm.renderTabs(); Lv.renderNav(); Lv.renderPicker();
  renderTop();
}
function undo() { active().undo(); renderTop(); }
function redo() { active().redo(); renderTop(); }

// the iOS app, built with what's saved, on every running iOS Simulator
async function simulator() {
  const count = store.levels.filter(levelDirty).length + store.gauntlets.filter(gauntletDirty).length;
  if (count) {
    if (!(await ask({ title: 'Save first?', ok: 'Save, then build', body: `The iOS app is built with what’s saved: your ${count} unsaved edit${count === 1 ? '' : 's'} would be left out.` }))) return;
    await save();
    if (store.levels.some(levelDirty) || store.gauntlets.some(gauntletDirty)) return;
  }
  try { await api('POST', '/api/simulator', {}); } catch (e) { return toast(e.message, 'bad'); }
  pollSimulator();
}
async function pollSimulator() {
  let st;
  try { st = await api('GET', '/api/simulator'); } catch (e) { st = { state: 'failed', step: e.message }; }
  const job = $('job');
  job.hidden = st.state !== 'running';
  job.textContent = `iOS: ${st.step}…`;
  job.dataset.tip = st.step;
  $('sim').disabled = st.state === 'running';
  if (st.state === 'running') return setTimeout(pollSimulator, 1500);
  if (st.state === 'done') toast(`Installed on ${st.devices.join(' and ')}: in the game, Developer menu → Course level, then Jump to gauntlet.`, 'ok');
  else if (st.state === 'failed') toast(`The Simulator build failed: ${st.step}`, 'bad');
}

$('undo').onclick = undo;
$('redo').onclick = redo;
$('save').onclick = save;
$('revert').onclick = () => unsaved('revert');
$('dirty').onclick = () => unsaved('review');
$('help').onclick = () => window.open('/wiki/', 'cuckoo-level-editor-wiki');
$('play').onclick = () => active().play(false);
$('watch').onclick = () => active().play(true);
$('sim').onclick = simulator;

// ---- the keyboard: the shell's keys, then the view's
window.addEventListener('keydown', (e) => {
  const mod = e.metaKey || e.ctrlKey, k = e.key.toLowerCase();
  if (mod && k === 's') { e.preventDefault(); save(); return; }
  if (e.target.closest && e.target.closest('textarea, input, select')) return;
  if (mod && k === 'z') { e.preventDefault(); e.shiftKey ? redo() : undo(); return; }
  if (mod && k === 'y') { e.preventDefault(); redo(); return; }
  if (!mod && !e.altKey && (e.key === 'Tab')) return;
  if (!mod && (k === 'g' && e.shiftKey)) { $('modes').querySelector(`[data-mode="${mode === 'level' ? 'gauntlet' : 'level'}"]`).click(); return; }
  active().keydown(e);
});
window.addEventListener('keyup', (e) => active().keyup(e));
window.addEventListener('resize', () => active().resize());
window.addEventListener('beforeunload', (e) => { if (store.levels.some(levelDirty) || store.gauntlets.some(gauntletDirty)) { e.preventDefault(); e.returnValue = ''; } });

// ---- start
function route() { // the address: #level/13, #gauntlet/tomb (#tomb too)
  const hash = decodeURIComponent(location.hash.slice(1));
  const [kind, which] = hash.includes('/') ? hash.split('/') : [gauntletNamed(hash) ? 'gauntlet' : '', hash];
  if (kind === 'gauntlet' && gauntletNamed(which)) { showGauntlet(which); return true; }
  if (kind === 'level' && levelNumbered(Number(which))) { showLevel(Number(which)); return true; }
  return false;
}
// the page's own buttons: their tips (tips.js)
function staticTips() {
  const tips = { undo: 'top.undo', redo: 'top.redo', revert: 'top.revert', save: 'top.save', dirty: 'top.unsaved', play: 'top.play', watch: 'top.watch', sim: 'top.sim', help: 'top.help',
    lstartChip: 'top.start', gstartChip: 'top.start', zoomIn: 'view.zoom', zoomOut: 'view.zoom', zoomFit: 'view.zoom', lzoomIn: 'view.zoom', lzoomOut: 'view.zoom', lzoomFit: 'view.zoom',
    rpPlay: 'greplay.play', rpPrev: 'greplay.prev', rpNext: 'greplay.next', rpSpeed: 'greplay.speed', rpRate: 'greplay.rate', track: 'greplay.track', minimap: 'gview.minimap',
    lrpPlay: 'lreplay.play', lrpStart: 'lreplay.start', lrpFollow: 'lreplay.follow', lrpSpeed: 'lreplay.speed', ltrack: 'lreplay.track', lminimap: 'lview.minimap' };
  for (const [id, key] of Object.entries(tips)) setTip($(id), key);
  for (const b of $('modes').querySelectorAll('button')) setTip(b, b.dataset.mode === 'level' ? 'view.levels' : 'view.gauntlets');
}
async function start() {
  initTooltips();
  staticTips();
  let levels, grids;
  let kept;
  let rules;
  try { [levels, grids, kept, rules] = await Promise.all([api('GET', '/api/levels'), api('GET', '/api/gauntlets'), H.loadAll(), api('GET', '/api/rules')]); } catch (e) { $('loading').textContent = 'The editor’s server isn’t answering: ' + e.message; return; }
  $('loading').remove();
  K.useRules(rules); // (the game's forecourt, lintel and porch: else the editor's own)
  const restored = [];
  store.gauntlets = grids.gauntlets.map((g) => Gm.gauntlet(g));
  for (const g of store.gauntlets) if (Gm.restoreDraft(g)) restored.push(`the ${g.name}`);
  for (const g of Gm.restoreNew()) { store.gauntlets.push(g); restored.push(`the ${g.name} (new)`); }
  store.levels = levels.levels.map((l) => Lv.level(l.number, l.text));
  for (const lv of store.levels) if (Lv.restoreDraft(lv)) restored.push(`level ${lv.number}`);
  Gm.init(kept);
  Lv.init(kept);
  Lv.checkAll();
  if (!route()) { // (else where you were)
    if (local.get('cuckoo-editor-mode') === 'gauntlet' && gauntletNamed(local.get('cuckoo-gauntlet-current'))) showGauntlet(local.get('cuckoo-gauntlet-current'));
    else showLevel(levelNumbered(local.get('cuckoo-level-current')) ? local.get('cuckoo-level-current') : store.levels[0].number);
  }
  window.addEventListener('hashchange', route);
  if (restored.length) toast(`Unsaved edits brought back: ${list(restored)}`);
  renderTop();
  api('GET', '/api/simulator').then((st) => { if (st.state === 'running') pollSimulator(); }).catch(() => {});
}
start();
window.levelEditor = { store, levels: Lv.debug, gauntlets: Gm.debug, showLevel, showGauntlet, save, esc }; // (the console, and the editor's own checks)
