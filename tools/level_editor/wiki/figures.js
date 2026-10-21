// The wiki's pictures, drawn by the level editor's own code: its page's controls (the editor's page, fetched, its
// buttons cloned), its tool lists and tooltips (tools.js, tips.js, tooltip.js), its canvases (CourseView, GridView)
// drawing small sample courses read by its own parser, its History card and the words its describers write. So they
// look as the editor does, today. Each element with data-fig gets its picture; each .tipref (id "tip-<key>") the
// control that tips.js key is on, and what its tooltip says. The canvases animate while they're on screen.
//
// Adding a picture: a data-fig="<name>" element in index.html and a FIGS.<name> here; a new tips.js key: a .tipref
// with its id in index.html (tests/wiki.test.mjs checks there's one), and a case in visual() if it needs a new kind of
// control.
import { TIPS, CELL_TIP, setTip } from '/static/js/tips.js';
import { LEVEL_TOOLS, GAUNTLET_TOOLS, KINDS, LEVEL_OVERLAYS, GAUNTLET_OVERLAYS } from '/static/js/tools.js';
import { CourseView, ZONE_LOOK, K_COLORS, RULER_TOP, RULER_LEFT } from '/static/js/courseview.js';
import { GridView, C as GC, RULER_TOP as G_TOP, RULER_LEFT as G_LEFT } from '/static/js/view.js';
import * as K from '/static/js/course.js';
import * as G from '/static/js/grid.js';
import * as D from '/static/js/describe.js';
import * as H from '/static/js/history.js';
import { renderHistory } from '/static/js/historycard.js';
import { initTooltips } from '/static/js/tooltip.js';
import { el, esc, titled } from '/static/js/util.js';

const U = K.U;
const $$ = (sel, root = document) => [...root.querySelectorAll(sel)];
const clamp = (v, a, b) => Math.max(a, Math.min(b, v));
const svg = (paths, cls = '') => `<svg viewBox="0 0 24 24"${cls ? ` class="${cls}"` : ''}>${paths}</svg>`;
const STILL = matchMedia('(prefers-reduced-motion: reduce)').matches;

// ---- the samples: small courses, read by the editor's own parser (course.js), and a small maze gauntlet
function level({ length, walls, saws = [], discos = [] }) { // walls: [cells, high, kind, barrier?] | [cells, 0, 'gauntlet', name]
  const lines = [`length ${length}`, 'difficulty 420 0.35', 'raids auto', 'course', ...walls.map((w) => w.join(' '))];
  if (saws.length) lines.push('saws', saws.join(' '));
  if (discos.length) lines.push('discos', ...discos.map((d) => d.join(' ')));
  return K.parseLevel(lines.join('\n') + '\n');
}

// The sample gauntlet: in by the door on the left, an egg for the saw in the floor, through a gate, turned back by a
// bumper and back again by the gate, an egg to climb the bumper, under hanging cubes, an egg for the cube saw, out
const SAMPLE = 'sample';
const GRID = [
  '#############################',
  '#...........................#',
  '#.......................L...#',
  '#.......................L...#',
  '#.................HHHH......#',
  '#.........R..................',
  '..........R.................X',
  '>.........R...B########C#####',
  '####S########################',
];
const GRIDS = { [SAMPLE]: GRID };
const GRID_INFO = G.doors(GRID);
const STEPS = (() => { // the bot's way: [col, the row under her stack, eggs, way, laid one here]
  const out = [], go = (c, b, n, d, lay = 0) => out.push([c, b, n, d, lay]);
  for (let c = 0; c <= 2; c++) go(c, 1, 0, 1);
  go(3, 1, 1, 1, 1); go(4, 1, 1, 1);
  for (let c = 5; c <= 13; c++) go(c, 1, 0, 1);
  for (let c = 13; c >= 11; c--) go(c, 1, 0, -1);
  go(11, 1, 0, 1); go(12, 1, 1, 1, 1); go(13, 1, 1, 1); go(14, 2, 0, 1);
  for (let c = 15; c <= 21; c++) go(c, 2, 0, 1);
  go(22, 2, 1, 1, 1); go(23, 2, 1, 1);
  for (let c = 24; c <= 28; c++) go(c, 2, 0, 1);
  return out;
})();
const RUN = { steps: STEPS, nearMisses: [[4, 1, 1], [14, 2, 0], [19, 2, 2], [23, 2, 1]], crash: null };
const CELL_MAP = (() => { // where her feet get: along her way (and a stack's height over it), a dead end in the corner
  const map = GRID.map((r) => [...r].map(() => '')), n = GRID.length;
  for (const [c, b] of STEPS) for (let k = 0; k < 3; k++) { const i = n - 1 - b - k; if (i > 0 && GRID[i][c] === '.') map[i][c] = 'w'; }
  for (let i = 1; i <= 3; i++) for (let c = 1; c <= 3; c++) map[i][c] = 'd';
  return map;
})();
const CHANGED = new Set(['18,4', '19,4', '20,4', '21,4']);

// A careful run along a course (the bot's, near enough, for the pictures): [x, her floor (metres), eggs under her],
// an egg laid as each climb or blade ahead needs it
function careful(L, x0, x1, step = 0.1) {
  const h = (x) => K.heightAt(L, x), out = [];
  for (let x = x0; x <= x1 + 1e-9; x += step) {
    const floor = h(x);
    let n = 0;
    for (let d = 0.05; d <= 2.4; d += 0.05) { const rise = h(x + d) - floor; if (rise > 0) n = Math.max(n, rise - Math.floor(d / 0.3)); }
    for (const s of L.saws) if (s - x > 0 && s - x < 0.75 && h(s) === floor) n = Math.max(n, 1);
    out.push([Math.round(x * 100) / 100, floor * U, n]);
  }
  return out;
}
const henOn = (tr, t, secs) => { const p = tr[Math.floor(((t % secs) / secs) * tr.length)]; return { x: p[0], base: p[1], eggs: p[2] }; };

const OVERVIEW = level({ length: 51, walls: [[6, 0, 'crate'], [4, 2, 'crate'], [3, 4, 'crate'], [5, 0, 'crate'], [5, 3, 'hay'], [4, 3, 'mix', 7], [4, 0, 'crate'], [10, 1, 'crate'], [3, 1, 'crate', 3], [29, 0, 'gauntlet', SAMPLE], [6, 2, 'crate'], [3, 0, 'crate'], [8, 0, 'crate']], saws: [6.9, 11.4, 13.2], discos: [[3, 2.2], [9.3, 3.7], [49, 2.6]] });
const OVERVIEW_GAME = { trace: careful(OVERVIEW, 0, 26.2), events: [[6, 2.4, 0], [15, 3.2, 1]], cleared: true };
const RUNL = level({ length: 30, walls: [[5, 0, 'crate'], [4, 2, 'crate'], [3, 3, 'hay'], [4, 0, 'crate'], [5, 4, 'crate'], [3, 2, 'mix', 6], [5, 0, 'crate'], [4, 3, 'hay'], [6, 0, 'crate'], [22, 0, 'crate']], saws: [8.1, 20.4], discos: [[11.1, 3.3], [24, 1.8]] });
const RUN_TRACE = careful(RUNL, 0, 29.4);
const RUN_EVENTS = [[9.6, 2.4, 0], [13.5, 2.4, 1], [17.4, 1.8, 0]];
const WALK = (() => { // a careful player's meter along it (the checker's egg budget): full, refilling, an egg a climb
  let meter = 8, prev = 0, px = 0;
  return RUN_TRACE.map(([x, , n]) => { meter = Math.min(8, meter + (x - px) / 4.96); if (n > prev) meter -= n - prev; prev = n; px = x; return [x, Math.max(0, meter)]; });
})();

// ---- what the pictures draw over the editor's: a label with a leader line, a dimension
function rr(ctx, x, y, w, h, r) { ctx.beginPath(); ctx.moveTo(x + r, y); ctx.arcTo(x + w, y, x + w, y + h, r); ctx.arcTo(x + w, y + h, x, y + h, r); ctx.arcTo(x, y + h, x, y, r); ctx.arcTo(x, y, x + w, y, r); ctx.closePath(); }
function tag(view, x, y, text, { color = '#f4ff5a', lift = 30, left = RULER_LEFT, top = RULER_TOP } = {}) {
  const ctx = view.ctx;
  ctx.save();
  ctx.font = '700 11px "Chakra Petch", sans-serif';
  const w = ctx.measureText(text).width + 14, h = 20;
  const lx = clamp(x - w / 2, left + 4, view.w - w - 4), ly = clamp(y - lift - h, top + 4, view.h - h - 4);
  ctx.strokeStyle = color; ctx.globalAlpha = 0.75; ctx.lineWidth = 1; ctx.setLineDash([3, 3]);
  if (ly + h < y - 4) { ctx.beginPath(); ctx.moveTo(x, ly + h); ctx.lineTo(x, y - 3); ctx.stroke(); }
  ctx.setLineDash([]); ctx.globalAlpha = 1;
  ctx.fillStyle = 'rgba(12,7,22,0.94)'; rr(ctx, lx, ly, w, h, 6); ctx.fill();
  ctx.strokeStyle = color; rr(ctx, lx + 0.5, ly + 0.5, w - 1, h - 1, 6); ctx.stroke();
  ctx.fillStyle = color; ctx.textAlign = 'center'; ctx.textBaseline = 'middle'; ctx.fillText(text, lx + w / 2, ly + h / 2 + 0.5);
  ctx.restore();
}
function dimension(view, x0, y0, x1, y1, text, color = '#f4ff5a') { // |<- text ->|, along or up
  const ctx = view.ctx, along = Math.abs(y1 - y0) < 1;
  ctx.save(); ctx.strokeStyle = color; ctx.fillStyle = color; ctx.lineWidth = 1.3;
  ctx.beginPath(); ctx.moveTo(x0, y0); ctx.lineTo(x1, y1);
  if (along) { ctx.moveTo(x0, y0 - 5); ctx.lineTo(x0, y0 + 5); ctx.moveTo(x1, y1 - 5); ctx.lineTo(x1, y1 + 5); } else { ctx.moveTo(x0 - 5, y0); ctx.lineTo(x0 + 5, y0); ctx.moveTo(x1 - 5, y1); ctx.lineTo(x1 + 5, y1); }
  ctx.stroke();
  ctx.font = '700 11px "Chakra Petch", sans-serif';
  if (along) { ctx.textAlign = 'center'; ctx.textBaseline = 'bottom'; ctx.fillText(text, (x0 + x1) / 2, y0 - 5); } else { ctx.textAlign = 'left'; ctx.textBaseline = 'middle'; ctx.fillText(text, x0 + 8, (y0 + y1) / 2); }
  ctx.restore();
}

// ---- the canvases animate while they're on screen (one loop for them all)
const live = new Map(); // canvas -> { draw(t), on }
const seen = new IntersectionObserver((es) => { for (const e of es) { const f = live.get(e.target); if (f) f.on = e.isIntersecting; } kick(); }, { rootMargin: '160px' });
let raf = 0;
function animate(canvas, draw) { live.set(canvas, { draw, on: false }); seen.observe(canvas); }
function kick() { if (!raf) raf = requestAnimationFrame(loop); }
function loop(now) {
  raf = 0;
  let any = false;
  for (const f of live.values()) {
    if (!f.on) continue;
    if (STILL && f.drawn) continue;
    f.draw(STILL ? 2.2 : now / 1000);
    f.drawn = true;
    any = true;
  }
  if (any && !STILL) kick();
}

// A course canvas, as the Levels view draws it: a scene ({ zone, L, cols: [c0, c1] framed, rows, overlays, game,
// check, diff, state(t): what changes as it plays, note(view, t, s): labels, chip(t): where Play starts })
function courseFigure(canvas, scene, mini = null) {
  const view = new CourseView(canvas, mini || document.createElement('canvas'));
  let sized = false, chip = null, said = '';
  const fit = () => {
    view.resize();
    const [c0, c1] = scene.cols, n = c1 - c0;
    view.z = clamp(Math.floor(Math.min((view.w - RULER_LEFT - 24) / n, (view.h - RULER_TOP - 30) / scene.rows)), 5, 26);
    view.ox = RULER_LEFT + Math.max(8, (view.w - RULER_LEFT - n * view.z) / 2) - c0 * view.z;
    const used = scene.rows * view.z; // (its rows centred up and down, the street no lower than near the bottom)
    view.oy = Math.round(Math.min(view.h - 22, RULER_TOP + 8 + (view.h - RULER_TOP - 8 - used) / 2 + used));
    sized = true;
  };
  new ResizeObserver(() => { sized = false; }).observe(canvas);
  animate(canvas, (t) => {
    if (!sized) fit();
    const s = { L: scene.L, zone: scene.zone, time: t, grids: GRIDS, game: scene.game, check: scene.check, diff: scene.diff,
      overlays: { grid: false, bot: false, notes: false, changes: false, ...scene.overlays }, ...(scene.state ? scene.state(t) : {}) };
    view.draw(s);
    if (mini) view.minimap(s);
    if (scene.note) scene.note(view, t, s);
    if (scene.chip) {
      if (!chip) chip = startChip(canvas);
      const where = typeof scene.chip === 'function' ? scene.chip(t) : scene.chip;
      if (where !== said) { chip.querySelector('.where').textContent = where; said = where; }
    }
  });
  return view;
}
// A grid canvas, as the Gauntlets view draws it (the sample gauntlet): a scene ({ overlays, state(t), note })
function gridFigure(canvas, scene, mini = null, rows = GRID) {
  const view = new GridView(canvas, mini || document.createElement('canvas'));
  view.rows = rows;
  let sized = false;
  const fit = () => {
    view.resize();
    const n = G.cols(rows), m = rows.length;
    view.z = clamp(Math.floor(Math.min((view.w - G_LEFT - 20) / n, (view.h - G_TOP - 16) / m)), 5, 40);
    view.ox = G_LEFT + Math.max(8, (view.w - G_LEFT - n * view.z) / 2);
    view.oy = G_TOP + Math.max(6, (view.h - G_TOP - m * view.z) / 2);
    sized = true;
  };
  new ResizeObserver(() => { sized = false; }).observe(canvas);
  animate(canvas, (t) => {
    if (!sized) fit();
    const s = { rows, lints: [], overlays: { grid: true, path: false, misses: false, reach: false, changes: false, ...scene.overlays }, ...(scene.state ? scene.state(t) : {}) };
    view.draw(s);
    if (mini) view.minimap(rows, RUN, true);
    if (scene.note) scene.note(view, t, s);
  });
  return view;
}
// where Play starts, the chip in the canvas's corner (the editor's own, cloned)
function startChip(canvas) {
  let box = canvas.parentElement;
  if (!box.classList.contains('stagebox')) { box = el('div', 'stagebox'); canvas.replaceWith(box); box.append(canvas); }
  const chip = part('lstartChip');
  setTip(chip, 'top.start');
  box.append(chip);
  return chip;
}

// ---- the scenes the course canvases show
const flip = (t, secs) => Math.floor(t / secs) % 2 === 1;
const COURSE = {
  overview: () => ({ zone: 'egypt', L: OVERVIEW, cols: [0, 90], rows: 12, overlays: { bot: true }, game: OVERVIEW_GAME,
    state: (t) => ({ replay: henOn(OVERVIEW_GAME.trace, t, 9) }) }),
  walls: () => {
    const L = level({ length: 99, walls: [[4, 0, 'crate'], [3, 2, 'crate'], [4, 3, 'hay'], [3, 4, 'mix'], [12, 0, 'crate']] });
    const hOf = (t) => 1 + (Math.floor(t * 1.3) % 5);
    return { zone: 'city', L, cols: [0, 24], rows: 8,
      state: (t) => { const h = hOf(t); return { preview: { kind: 'wall', cols: [16, 17, 18, 19].map((c) => ({ c, h, kind: 'crate' })) }, hover: { c: 19, r: h - 1, x: 19.5 * U } }; },
      note: (v, t) => { tag(v, v.X(5.5), v.Y(2), 'crates', { lift: 16 }); tag(v, v.X(9), v.Y(3), 'hay', { lift: 40 }); tag(v, v.X(12.5), v.Y(4), 'hay-topped', { lift: 16 }); tag(v, v.X(18), v.Y(hOf(t)), `the brush: ${hOf(t)} high`, { color: '#ffffff', lift: 20 }); } };
  },
  barrier: () => {
    const L = level({ length: 99, walls: [[4, 0, 'crate'], [3, 1, 'crate'], [6, 2, 'hay', 6], [3, 1, 'crate'], [8, 0, 'crate']] });
    const tr = careful(L, 0.3, 13.2), fit = Math.max(0, Math.floor((6 * U - 1.05 - 0.02) / U + 1e-6) - 2);
    return { zone: 'egypt', L, cols: [0, 24], rows: 9, state: (t) => ({ replay: henOn(tr, t, 6) }),
      note: (v) => { const x = v.X(13) + 10; dimension(v, x, v.Y(2), x, v.Y(6), `${fit} eggs fit under it`, '#9ef6ff'); } };
  },
  saws: () => {
    const L = level({ length: 99, walls: [[3, 0, 'crate'], [5, 2, 'crate'], [3, 0, 'crate'], [10, 1, 'hay'], [5, 0, 'crate']], saws: [3.3, 7.8, 9.6, 11.4] });
    const blades = [7.8, 9.6, 11.4];
    return { zone: 'japan', L, cols: [0, 26], rows: 8,
      state: (t) => { const x = 5.6 + ((t % 4.5) / 4.5) * 8.2; return { replay: { x, base: K.heightAt(L, x) * U, eggs: x < 6.6 ? 3 : Math.max(0, 3 - blades.filter((s) => x > s).length) } }; },
      note: (v) => { tag(v, v.X(3.3 / U), v.Y(2), 'a saw'); dimension(v, v.X(7.8 / U), v.Y(1) - 24, v.X(9.6 / U), v.Y(1) - 24, 'a blade’s gap: 1.8 m', '#ff8a6a'); tag(v, v.X(11.4 / U), v.Y(1), 'cube saws (on hay)', { lift: 46 }); } };
  },
  columns: () => {
    const L = level({ length: 99, walls: [[3, 0, 'crate'], [4, 2, 'crate'], [3, 3, 'hay'], [4, 1, 'crate'], [8, 0, 'crate']], saws: [4.5] });
    return { zone: 'city', L, cols: [0, 22], rows: 8, state: (t) => ({ preview: { kind: 'cols', c0: 7, c1: 9, remove: flip(t, 2.2) } }),
      note: (v, t) => tag(v, v.X(8.5), v.Y(3), flip(t, 2.2) ? '⌥: taken out, what’s after moves back' : 'copied in after itself, what’s after moves on', { color: flip(t, 2.2) ? '#ff6a80' : '#f4ff5a' }) };
  },
  gauntlet: () => {
    const L = level({ length: 99, walls: [[4, 0, 'crate'], [8, 1, 'crate'], [3, 1, 'crate', 3], [29, 0, 'gauntlet', SAMPLE], [8, 2, 'crate'], [4, 0, 'crate']] });
    return { zone: 'egypt', L, cols: [0, 56], rows: 12,
      state: (t) => {
        if (!flip(t, 2.6)) return { hoverSpan: SAMPLE };
        const c = 15 + Math.round(1 + Math.sin(t * 2) * 1.5), fp = K.gauntletFootprint(GRID_INFO, c);
        return { preview: { kind: 'gauntlet', a: fp.a, c, z: fp.z, grid: GRID_INFO, ok: true } };
      },
      note: (v) => { tag(v, v.X(8), v.Y(1), 'forecourt'); tag(v, v.X(13.5), v.Y(3), 'lintel', { lift: 22 }); tag(v, v.X(48), v.Y(2), 'porch'); } };
  },
  start: () => {
    const L = level({ length: 99, walls: [[4, 0, 'crate'], [4, 2, 'crate'], [4, 0, 'crate'], [3, 3, 'hay'], [9, 0, 'crate']], saws: [8.4] });
    return { zone: 'city', L, cols: [0, 24], rows: 9, chip: 'from 7.2 m', state: (t) => ({ start: { x: 7.2 }, hoverStart: flip(t, 1.6) }),
      note: (v) => dimension(v, v.X(12), v.Y(3) + 10, v.X(14), v.Y(3) + 10, '1.2 m clear', '#3dff8a') };
  },
  mazestart: () => {
    const L = level({ length: 99, walls: [[3, 0, 'crate'], [5, 1, 'crate'], [3, 1, 'crate', 3], [29, 0, 'gauntlet', SAMPLE], [4, 2, 'crate']] });
    const dir = (t) => (flip(t, 2) ? -1 : 1);
    return { zone: 'egypt', L, cols: [4, 44], rows: 11, chip: (t) => `from the ${titled(SAMPLE)}, col 16 row 2 ${dir(t) < 0 ? '←' : '→'}`,
      state: (t) => ({ start: { gauntlet: SAMPLE, c: 16, b: 2, dir: dir(t) } }) };
  },
  bot: () => {
    const cut = RUN_TRACE.findIndex((p) => p[0] >= 20.3);
    return { zone: 'city', L: RUNL, cols: [0, 51], rows: 9, overlays: { bot: true },
      state: (t) => { const crashed = flip(t, 7), game = { trace: crashed ? RUN_TRACE.slice(0, cut) : RUN_TRACE, events: crashed ? RUN_EVENTS.slice(0, 2) : RUN_EVENTS, cleared: !crashed }; return { game, replay: henOn(game.trace, t, crashed ? 4.9 : 7) }; } };
  },
  changes: () => {
    const A = level({ length: 31.2, walls: [[4, 0, 'crate'], [4, 2, 'crate'], [3, 3, 'hay'], [6, 0, 'crate'], [4, 2, 'crate'], [39, 0, 'crate']], saws: [4.2, 12], discos: [[7.8, 3]] });
    let B = A; // (the hay raised, two columns copied in, a saw off and one on, a disco ball, the finish moved on)
    for (let c = 8; c <= 10; c++) B = K.setHeight(B, c, 4, 'hay');
    B = K.duplicateCols(B, 17, 18);
    B = K.removeSaw(B, 0);
    B = K.addSaw(B, 14.4);
    B = K.addDisco(B, 3.3, 2.6);
    B = K.setLength(B, 33.6);
    const diff = D.diffCourse(A, B);
    return { zone: 'japan', L: B, cols: [0, 60], rows: 9, overlays: { changes: true }, diff, summary: D.summarizeCourse(diff) };
  },
};
// ...and the pieces and hazards, one each
const HAZARD = {
  saw: () => ({ zone: 'city', L: level({ length: 99, walls: [[4, 0, 'crate'], [6, 2, 'crate'], [6, 0, 'crate']], saws: [4.2] }), cols: [0, 16], rows: 6 }),
  'cube-saw': () => ({ zone: 'egypt', L: level({ length: 99, walls: [[4, 0, 'crate'], [6, 2, 'hay'], [6, 0, 'crate']], saws: [4.2] }), cols: [0, 16], rows: 6 }),
  barrier: () => ({ zone: 'egypt', L: level({ length: 99, walls: [[4, 0, 'crate'], [6, 1, 'mix', 5], [6, 0, 'crate']] }), cols: [0, 16], rows: 7,
    note: (v) => { const x = v.X(10) + 8; dimension(v, x, v.Y(1), x, v.Y(5), '2 eggs fit', '#9ef6ff'); } }),
  disco: () => ({ zone: 'japan', L: level({ length: 99, walls: [[5, 0, 'crate'], [4, 1, 'hay'], [7, 0, 'crate']], discos: [[3.9, 2.2], [7.2, 1.4]] }), cols: [0, 16], rows: 6 }),
  wall: () => ({ zone: 'city', L: level({ length: 99, walls: [[2, 0, 'crate'], [3, 2, 'crate'], [2, 0, 'crate'], [3, 3, 'hay'], [2, 0, 'crate'], [3, 4, 'mix'], [5, 0, 'crate']] }), cols: [0, 20], rows: 7 }),
  gauntlet: () => ({ zone: 'egypt', L: level({ length: 99, walls: [[3, 1, 'crate'], [29, 0, 'gauntlet', SAMPLE], [4, 2, 'crate']] }), cols: [0, 36], rows: 12, state: (t) => ({ hoverSpan: flip(t, 2.4) ? SAMPLE : null }) }),
  finish: () => ({ zone: 'city', L: level({ length: 6.6, walls: [[5, 0, 'crate'], [3, 1, 'crate'], [12, 0, 'crate']] }), cols: [0, 20], rows: 13, state: (t) => ({ hoverFinish: flip(t, 2) }) }),
  start: () => ({ zone: 'city', L: level({ length: 99, walls: [[4, 0, 'crate'], [5, 2, 'crate'], [9, 0, 'crate']] }), cols: [0, 18], rows: 7, state: (t) => ({ start: { x: 3.6 }, hoverStart: flip(t, 1.8) }) }),
  note: () => ({ zone: 'egypt', L: level({ length: 99, walls: [[4, 0, 'crate'], [3, 2, 'crate'], [9, 0, 'crate']] }), cols: [0, 16], rows: 6, overlays: { notes: true },
    check: { ok: true, notes: [{ x: 3.3, y: 2, text: '' }], short: [{ x: 6.6, y: 0, text: '' }], doors: [] } }),
};
// ...and the grid canvases
const GRID_SCENES = {
  cells: () => {
    const tour = [['.', 6, 3], ['#', 7, 8], ['H', 19, 4], ['B', 14, 7], ['R', 10, 6], ['L', 24, 3], ['S', 4, 8], ['C', 23, 7], ['>', 0, 7], ['X', 28, 6]];
    const at = (t) => tour[Math.floor(t / 1.5) % tour.length];
    return { overlays: { grid: true },
      state: (t) => { const [, c, i] = at(t); return { hover: { c, i } }; },
      note: (v, t) => { const [ch, c, i] = at(t); tag(v, v.ox + (c + 0.5) * v.z, v.oy + i * v.z, G.CELL[ch].name, { left: G_LEFT, top: G_TOP, lift: 12 }); } };
  },
  way: () => ({ overlays: { path: true, misses: true, reach: true, changes: true },
    state: (t) => ({ check: { ok: true, cellMap: CELL_MAP }, fresh: true, run: RUN, changed: CHANGED, start: { c: 16, b: 2, dir: 1 }, replay: { step: Math.floor(t * 5) % STEPS.length } }) }),
};

// ---- the editor's page: its controls, cloned (their ids taken off: they're the editor's)
let EDITOR = document.implementation.createHTMLDocument('');
let CONTENT = { levels: [], gauntlets: [] };
function strip(n) { n.removeAttribute('id'); for (const x of n.querySelectorAll('[id]')) x.removeAttribute('id'); n.hidden = false; return n; }
function partSel(sel, keepIds = false) {
  const n = EDITOR.querySelector(sel);
  if (!n) return el('span', 'missing', `(the editor has no ${esc(sel)})`);
  const c = document.importNode(n, true);
  c.hidden = false;
  return keepIds ? c : strip(c);
}
const part = (id) => partSel('#' + id);

const levelInfo = (n) => { // (a level of the campaign, as saved: its zone, its gauntlet)
  const lv = CONTENT.levels.find((l) => l.number === n), s = lv && K.spans(lv.L)[0];
  return { zone: K.zoneOf(n), gauntlet: s ? s.name : n === 13 ? 'gallery' : null, L: lv ? lv.L : null };
};
function navButton(which) { return el('button', 'icon', svg(which === 'prev' ? '<path d="M15 6l-6 6 6 6"/>' : '<path d="M9 6l6 6-6 6"/>')); }
function navCurrent(n = 13) {
  const { zone, gauntlet } = levelInfo(n);
  return el('button', `lv-current zone-${zone}`, `<span class="state ok"></span><span class="num">${n}</span><span class="meta"><span class="zone">${esc(K.ZONES[zone].name)}</span><span class="gname">${gauntlet ? `the ${esc(titled(gauntlet))}` : 'no gauntlet'}</span></span><span class="unsaved"></span>${svg('<path d="M6 9l6 6 6-6"/>', 'chev')}`);
}
function modes(on) { const m = partSel('.modes'); for (const b of m.querySelectorAll('button')) b.setAttribute('aria-selected', String(b.dataset.mode === on)); return m; }
function toolButton(tool) { return el('button', 'tool on', `${svg(tool.icon)}<span>${esc(tool.label)}</span>`); }
function overlayChip(o) { return el('button', 'chip on', `<span class="dot" style="background:${o.color}"></span>${esc(o.label)}`); }
function seg(labels, on) { return el('div', 'seg', labels.map((s, i) => `<button class="${i === on ? 'on' : ''}" tabindex="-1">${s}</button>`).join('')); }
function kindCanvas(kind, size = 28) { // (as the Levels view's swatches draw them: crates and hay from the course view)
  const cv = el('canvas');
  cv.width = cv.height = size * 2;
  const ctx = cv.getContext('2d'), z = (size - 2) / 2;
  ctx.scale(2, 2);
  ctx.fillStyle = '#0c0816'; ctx.fillRect(0, 0, size, size);
  for (let j = 0; j < 2; j++) for (let i = 0; i < 2; i++) CourseView.prototype.block.call({ ctx, z }, kind === 'hay' || (kind === 'mix' && j === 1) ? 'hay' : 'crate', 1 + i * z, size - 1 - (j + 1) * z, ZONE_LOOK.egypt, i, j);
  return cv;
}
function cellCanvas(ch, size = 28) { // (as the Gauntlets view's swatches draw them)
  const cv = el('canvas');
  cv.width = cv.height = size * 2;
  const ctx = cv.getContext('2d'), fake = { ctx, z: size };
  ctx.scale(2, 2);
  ctx.fillStyle = GC.air; ctx.fillRect(0, 0, size, size);
  GridView.prototype.cell.call(fake, ch, 0, 0, size);
  if ('#HSC'.includes(ch)) { ctx.strokeStyle = 'HC'.includes(ch) ? GC.cubeEdge : GC.stoneEdge; ctx.lineWidth = 2; ctx.strokeRect(1, 1, size - 2, size - 2); }
  if (ch === 'S' || ch === 'C') { ctx.save(); ctx.beginPath(); ctx.rect(0, 0, size, size); ctx.clip(); GridView.prototype.blade.call(fake, size / 2, size * 0.29, size * 0.79); ctx.restore(); }
  return cv;
}
function kindSwatch(kind) {
  const b = el('button', 'swatch' + (kind === 'crate' ? ' primary' : ''));
  b.append(kindCanvas(kind), el('span', 'k', KINDS.find((k) => k.id === kind).key));
  if (kind === 'crate') b.append(el('span', 'tag', 'ON'));
  return b;
}
function cellSwatch(ch) {
  const b = el('button', 'swatch' + (ch === '#' ? ' primary' : ch === '.' ? ' secondary' : ''));
  b.append(cellCanvas(ch), el('span', 'k', G.CELL[ch].key));
  if (ch === '#') b.append(el('span', 'tag', '1ST')); else if (ch === '.') b.append(el('span', 'tag', '2ND'));
  return b;
}

// the replay's track, as each view draws it (the run along the course; the run move by move)
function levelTrack(cv, t) {
  const ctx = sizeTo(cv), W = cv._w, Hh = cv._h, L = RUNL, len = Math.max(30, L.length), top = 14, bot = Hh - 14;
  const x = (m) => 8 + (m / len) * (W - 16), y8 = (v) => bot - (Math.min(v, 12) / 12) * (bot - top);
  ctx.fillStyle = '#0a0612'; ctx.fillRect(0, 0, W, Hh);
  ctx.fillStyle = 'rgba(255,255,255,0.05)';
  for (let c = 0; c < L.cols.length; c++) { const k = L.cols[c]; if (k.h) ctx.fillRect(x(c * U), bot - (k.h / 16) * (bot - top), Math.max(1, ((W - 16) * U) / len), (k.h / 16) * (bot - top)); }
  ctx.font = '500 10px "Chakra Petch"'; ctx.textBaseline = 'alphabetic';
  for (let m = 0; m <= len; m += 10) { ctx.fillStyle = 'rgba(255,255,255,0.06)'; ctx.fillRect(x(m), top, 1, bot - top); ctx.fillStyle = 'rgba(236,230,255,0.35)'; ctx.fillText(`${m}`, x(m) + 3, Hh - 3); }
  ctx.beginPath(); ctx.moveTo(x(0), bot); for (const [m, e] of WALK) ctx.lineTo(x(m), y8(e)); ctx.lineTo(x(WALK[WALK.length - 1][0]), bot); ctx.closePath();
  ctx.fillStyle = 'rgba(41,231,255,0.12)'; ctx.fill();
  ctx.strokeStyle = 'rgba(41,231,255,0.8)'; ctx.lineWidth = 1.2; ctx.beginPath(); WALK.forEach(([m, e], i) => (i ? ctx.lineTo(x(m), y8(e)) : ctx.moveTo(x(m), y8(e)))); ctx.stroke();
  ctx.strokeStyle = 'rgba(244,255,90,0.8)'; ctx.beginPath(); RUN_TRACE.forEach((p, i) => (i ? ctx.lineTo(x(p[0]), y8(p[2])) : ctx.moveTo(x(p[0]), y8(p[2])))); ctx.stroke();
  for (const [m, , kind] of RUN_EVENTS) { ctx.fillStyle = kind ? K_COLORS.call : K_COLORS.perfect; ctx.beginPath(); ctx.arc(x(m), 7, 2.5, 0, Math.PI * 2); ctx.fill(); }
  const p = henOn(RUN_TRACE, t, 7);
  ctx.fillStyle = '#fff'; ctx.fillRect(x(p.x) - 1, 2, 2, Hh - 4); ctx.beginPath(); ctx.moveTo(x(p.x) - 5, 2); ctx.lineTo(x(p.x) + 5, 2); ctx.lineTo(x(p.x), 8); ctx.fill();
  ctx.fillStyle = '#f4ff5a'; ctx.fillRect(x(L.length) - 1, top, 2, bot - top);
  if (W > 520) { ctx.textAlign = 'right'; ctx.fillStyle = 'rgba(41,231,255,0.75)'; ctx.fillText('a careful player’s meter', W - 8, top + 8); ctx.fillStyle = 'rgba(244,255,90,0.75)'; ctx.fillText('the bot’s stack', W - 8, top + 20); ctx.textAlign = 'left'; }
}
function gridTrack(cv, t) {
  const ctx = sizeTo(cv), W = cv._w, Hh = cv._h, n = STEPS.length, top = 12, bot = Hh - 8;
  const x = (k) => 8 + (k / (n - 1)) * (W - 16), y8 = (v) => bot - (Math.min(v, 8) / 8) * (bot - top);
  ctx.fillStyle = '#0a0612'; ctx.fillRect(0, 0, W, Hh);
  let meter = 2;
  const m = STEPS.map((s) => { meter = Math.min(8, meter + 0.32) - (s[4] ? 1 : 0); return meter; });
  ctx.beginPath(); ctx.moveTo(x(0), bot); m.forEach((e, k) => ctx.lineTo(x(k), y8(e))); ctx.lineTo(x(n - 1), bot); ctx.closePath(); ctx.fillStyle = 'rgba(41,231,255,0.12)'; ctx.fill();
  ctx.strokeStyle = 'rgba(41,231,255,0.8)'; ctx.lineWidth = 1.2; ctx.beginPath(); m.forEach((e, k) => (k ? ctx.lineTo(x(k), y8(e)) : ctx.moveTo(x(k), y8(e)))); ctx.stroke();
  ctx.strokeStyle = 'rgba(244,255,90,0.85)'; ctx.beginPath(); STEPS.forEach((s, k) => (k ? ctx.lineTo(x(k), y8(s[2])) : ctx.moveTo(x(k), y8(s[2])))); ctx.stroke();
  for (const [c, , kind] of RUN.nearMisses) { const k = STEPS.findIndex((s) => s[0] === c && s[3] > 0); ctx.fillStyle = [GC.perfect, GC.shave, GC.call][kind]; ctx.beginPath(); ctx.arc(x(k), 6, 2.5, 0, Math.PI * 2); ctx.fill(); }
  const k = Math.floor(t * 5) % n;
  ctx.fillStyle = '#fff'; ctx.fillRect(x(k) - 1, 2, 2, Hh - 4);
}
function sizeTo(cv) { // (a canvas at the screen's pixels; its CSS size on it as _w, _h)
  const r = cv.getBoundingClientRect(), dpr = window.devicePixelRatio || 1;
  if (cv.width !== Math.round(r.width * dpr) || cv.height !== Math.round(r.height * dpr)) { cv.width = Math.max(1, Math.round(r.width * dpr)); cv.height = Math.max(1, Math.round(r.height * dpr)); }
  const ctx = cv.getContext('2d');
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  cv._w = r.width; cv._h = r.height;
  return ctx;
}
function trackCanvas(isLevel) { const cv = el('canvas', 'track'); animate(cv, (t) => (isLevel ? levelTrack : gridTrack)(cv, t)); return cv; }
// the minimaps: the whole course (or grid), the box on it what's on screen, sweeping along
function courseMini() {
  const cv = el('canvas', 'minimap-fig'), view = new CourseView(document.createElement('canvas'), cv);
  let sized = false;
  new ResizeObserver(() => { sized = false; }).observe(cv);
  animate(cv, (t) => {
    if (!sized) { view.resize(); sized = true; }
    view.w = 520; view.h = 300; view.z = 14; view.rowsUp = 14;
    const left = ((t * 2.2) % 30) - 2;
    view.ox = RULER_LEFT - (left / U) * view.z;
    view.minimap({ L: OVERVIEW, grids: GRIDS, zone: 'egypt', game: OVERVIEW_GAME, overlays: { bot: true }, replay: henOn(OVERVIEW_GAME.trace, t, 9) });
  });
  return cv;
}
function gridMini() {
  const cv = el('canvas', 'minimap-fig'), view = new GridView(document.createElement('canvas'), cv);
  let sized = false;
  view.rows = GRID;
  new ResizeObserver(() => { sized = false; }).observe(cv);
  animate(cv, (t) => {
    if (!sized) { view.resize(); sized = true; }
    view.w = 320; view.h = 200; view.z = 26;
    view.ox = G_LEFT - (((t * 2) % 22) - 2) * view.z; view.oy = G_TOP - 2 * view.z;
    view.minimap(GRID, RUN, true);
  });
  return cv;
}

// ---- a .tipref: the control, its name and key, what its tooltip says
function visual(key) {
  const dot = key.indexOf('.'), group = key.slice(0, dot), id = key.slice(dot + 1);
  switch (group) {
    case 'view': return id === 'zoom' ? { el: partSel('#courseWrap .zoom') } : { el: modes(id === 'levels' ? 'level' : 'gauntlet'), wide: true };
    case 'top': {
      if (id === 'start') { const c = part('lstartChip'); c.querySelector('.where').textContent = 'from 212 m'; return { el: c, wide: true }; }
      const b = part({ undo: 'undo', redo: 'redo', unsaved: 'dirty', revert: 'revert', save: 'save', play: 'play', watch: 'watch', sim: 'sim', help: 'help' }[id]);
      if (id === 'unsaved') b.textContent = '3 unsaved';
      return { el: b };
    }
    case 'level': return id === 'picker' ? { el: navCurrent(), wide: true } : { el: navButton(id) };
    case 'ltool': return { el: toolButton(LEVEL_TOOLS.find((t) => t.tip === key)), dark: true };
    case 'gtool': return { el: toolButton(GAUNTLET_TOOLS.find((t) => t.tip === key)), dark: true };
    case 'kind': return { el: kindSwatch(id), dark: true };
    case 'cell': return { el: cellSwatch(Object.keys(CELL_TIP).find((ch) => CELL_TIP[ch] === key)), dark: true };
    case 'lover': return { el: overlayChip(LEVEL_OVERLAYS.find((o) => o.tip === key)) };
    case 'gover': return { el: overlayChip(GAUNTLET_OVERLAYS.find((o) => o.tip === key)) };
    case 'lreplay': case 'greplay': {
      const lv = group === 'lreplay';
      if (id === 'play') { const b = part(lv ? 'lrpPlay' : 'rpPlay'); if (!STILL) setInterval(() => b.classList.toggle('playing'), 1600); return { el: b }; }
      if (id === 'speed') return { el: seg(lv ? ['0.5×', '1×', '2×', '4×'] : ['0.25×', '0.5×', '1×'], lv ? 1 : 2), wide: true };
      if (id === 'rate') return { el: seg(['1×', '1.25×', '1.4×'], 0) };
      if (id === 'track') return { el: trackCanvas(lv), wide: true };
      const b = part({ start: 'lrpStart', follow: 'lrpFollow', prev: 'rpPrev', next: 'rpNext' }[id]);
      if (id === 'follow') b.classList.add('on');
      return { el: b };
    }
    case 'lview': return { el: courseMini(), wide: true };
    case 'gview': return { el: gridMini(), wide: true };
    case 'prop': return { el: prop(id), wide: true };
    case 'hazard': { const cv = el('canvas', 'scene'); courseFigure(cv, HAZARD[id]()); return { el: cv, wide: true }; }
    default: return null;
  }
}
function prop(id) { // (the Levels view's property rows, as renderProps lays them out)
  const box = el('div', 'props'), row = (label, ctl) => `<div class="prop"><label>${label}</label>${ctl}</div>`;
  const g = levelInfo(13).gauntlet || 'gallery';
  box.innerHTML = {
    finish: row('Finish line', '<div class="ctl"><input type="number" value="420" tabindex="-1"><span class="unit">m</span></div>'),
    difficulty: row('Difficulty', '<div class="ctl"><input type="number" value="420" tabindex="-1"><span class="unit">+</span><input type="number" value="0.35" tabindex="-1"><span class="unit">× m</span></div>')
      + '<div class="prop sub"><span></span><span class="readout-line">speed 12.4 m/s · an egg every 0.40 s</span></div>',
    raids: row('Droid raids', seg(['Auto', 'None', 1, 2, 3], 0).outerHTML),
    gauntlet: row('Gauntlet', `<div class="ctl"><select tabindex="-1"><option>${esc(titled(g))}</option></select></div>`),
    edit: '<div class="prop sub"><span></span><span class="btns"><button class="small" tabindex="-1">Edit its grid</button></span></div>',
    new: '<div class="prop sub"><span></span><span class="btns"><button class="small" tabindex="-1">New gauntlet…</button></span></div>',
  }[id] || '';
  return box;
}
function tipref(box) {
  const key = box.id.slice(4), t = TIPS[key];
  box.textContent = '';
  if (!t) { box.append(el('div', 'missing', `tips.js has no “${esc(key)}”`)); return; }
  const vis = el('div', 'vis'), v = visual(key);
  if (v && v.el) { vis.append(v.el); if (v.wide) box.classList.add('wide'); if (v.dark) vis.classList.add('dark'); for (const b of vis.querySelectorAll('button, input, select')) b.tabIndex = -1; }
  const what = el('div', 'what', `<div class="name"><b>${esc(t.title)}</b>${t.key ? t.key.split(' ').map((k) => `<kbd class="keycap">${esc(k)}</kbd>`).join('') : ''}</div><div class="txt">${esc(t.text)}</div><div class="key-id">tips.js · ${esc(key)}</div>`);
  box.append(vis, what);
}

// ---- the figures (data-fig)
const FIGS = {};

// the workflow: edit, check, play, save, commit
FIGS.flow = (box) => {
  const steps = [
    ['Edit', 'a level’s course or a gauntlet’s grid', 'var(--volt)', '<path d="M4 20l4-1 11-11-3-3L5 16z"/>'],
    ['Check', 'Ready to ship, as you edit', 'var(--green)', '<path d="M5 12.5l4.5 4.5L19 7"/>'],
    ['Play', 'you or the bot, from a start point', 'var(--cyan)', '<path d="M8 5l11 7-11 7z"/>'],
    ['Save', 'every edit, together', 'var(--pink)', '<path d="M5 3h11l3 3v15H5zM8 3v5h8V3M8 21v-7h8v7"/>'],
    ['Commit', 'the tests run first', 'var(--gold)', '<circle cx="12" cy="12" r="3.5"/><path d="M3 12h5.5M15.5 12H21"/>'],
  ];
  box.innerHTML = steps.map(([l, s, c, i], k) => `<div class="node" style="--c:${c};--i:${k}"><div class="disc">${svg(i)}</div><div><div class="lbl">${l}</div><div class="sub">${s}</div></div>${k < steps.length - 1 ? '<span class="wire"></span>' : ''}</div>`).join('');
};

// a map of each view: its parts numbered, each a link to its section
const MAP = {
  levels: [['#tip-view.levels', 'The view switch'], ['#level-picker', 'This level, the levels either side, every level'], ['#saving', 'Undo, redo, revert, save'], ['#testing', 'Play, watch the bot, the Simulator'],
    ['#level-tools', 'Wall kinds and the tools'], ['#tip-lview.minimap', 'The whole course'], ['#course', 'The course'], ['#level-overlays', 'Overlays'], ['#level-inspector', 'The inspector'], ['#level-replay', 'The bot’s run, replayed']],
  gauntlets: [['#tip-view.gauntlets', 'The view switch'], ['#gauntlet-tabs', 'A tab a gauntlet, and new ones'], ['#saving', 'Undo, redo, revert, save'], ['#testing', 'Play, watch the bot, the Simulator'],
    ['#cells', 'Cells and the tools'], ['#tip-gview.minimap', 'The whole gauntlet'], ['#gauntlets-view', 'The grid'], ['#gauntlet-overlays', 'Overlays'], ['#gauntlet-checks', 'The inspector: its checks'], ['#gauntlet-replay', 'The replay']],
};
FIGS.map = (fig) => {
  const lv = fig.dataset.view === 'levels', P = MAP[lv ? 'levels' : 'gauntlets'];
  const reg = (n, cls, inner) => `<a class="mreg ${cls}" href="${P[n - 1][0]}" data-n="${n}" aria-label="${n}: ${esc(P[n - 1][1])}"><span class="mnum" style="--i:${n}">${n}</span>${inner}</a>`;
  const pill = (txt, cls = '') => `<span class="pill ${cls}">${txt}</span>`;
  const icons = EDITOR.querySelectorAll('.modes svg');
  const nav = lv ? pill('‹') + pill('13', 'num') + pill('›')
    : ['tomb', 'gallery', 'wells'].map((g, i) => pill(`<i></i>${titled(g)}`, 'tabp' + (i === 1 ? ' on' : ''))).join('') + pill('+ New gauntlet', 'tabp');
  const chips = (lv ? LEVEL_OVERLAYS : GAUNTLET_OVERLAYS).map((o) => `<span><i style="background:${o.color}"></i>${esc(o.label)}</span>`).join('');
  const cards = (lv ? ['Ready to ship', 'History', 'The level', 'In the game', 'The course', 'Description'] : ['Ready to ship', 'History', 'The numbers', 'In the game', 'Near misses', 'Description'])
    .map((c, i) => `<div class="mcard">${c}${i ? '<div class="bar"></div>' : '<div><span class="ok">READY</span></div>'}</div>`).join('');
  fig.insertAdjacentHTML('beforeend', `<div class="mwrap"><div class="mframe">
    <div class="mtop"><div class="brand"><div class="mark">CUCKOO <b>STACK</b></div></div>
      ${reg(1, '', pill((icons[0] ? icons[0].outerHTML : '') + 'Levels', lv ? 'on' : '') + pill((icons[1] ? icons[1].outerHTML : '') + 'Gauntlets', lv ? '' : 'on'))}
      ${reg(2, '', nav)}<span class="grow"></span>
      ${reg(3, '', pill(svg('<path d="M9 7H4V2M4 7a9 9 0 1 1-1.8 5.6"/>')) + pill(svg('<path d="M15 7h5V2M20 7a9 9 0 1 0 1.8 5.6"/>')) + pill('Revert') + pill('Save', 'pink'))}
      ${reg(4, '', pill(svg('<path d="M7 4l13 8-13 8z"/>') + 'Play', 'cyan') + pill('Watch the bot') + pill('Simulator'))}
    </div>
    ${reg(5, 'mtools', `<div class="sw"></div>${(lv ? LEVEL_TOOLS : GAUNTLET_TOOLS).map((t, i) => svg(t.icon, i ? '' : 'on')).join('')}`)}
    ${reg(6, 'mmini', '<canvas></canvas>')}
    <div class="mstagewrap">${reg(7, 'mstage', '<canvas></canvas>')}${reg(8, 'mchips', chips)}</div>
    ${reg(9, 'minsp', cards)}
    ${reg(10, 'mfoot', `<span class="rnd">${svg('<path d="M8 5l11 7-11 7z"/>')}</span><canvas></canvas>`)}
  </div></div>
  <ol class="mlegend">${P.map(([href, label], i) => `<li><a href="${href}" data-n="${i + 1}"><span class="mnum">${i + 1}</span>${esc(label)}</a></li>`).join('')}</ol>`);
  const sw = fig.querySelector('.sw');
  if (lv) KINDS.forEach((k, i) => { const c = kindCanvas(k.id, 20); if (!i) c.classList.add('on'); sw.append(c); });
  else G.CELLS.forEach((c, i) => { const cv = cellCanvas(c.ch, 20); if (i === 1) cv.classList.add('on'); sw.append(cv); });
  const stage = fig.querySelector('.mstage canvas'), mini = fig.querySelector('.mmini canvas');
  if (lv) courseFigure(stage, { ...COURSE.overview(), cols: [0, 64] }, mini);
  else gridFigure(stage, GRID_SCENES.way(), mini);
  const track = fig.querySelector('.mfoot canvas');
  animate(track, (t) => (lv ? levelTrack : gridTrack)(track, t));
  const light = (n, on) => { for (const a of fig.querySelectorAll(`[data-n="${n}"]`)) a.classList.toggle('lit', on); };
  for (const a of fig.querySelectorAll('[data-n]')) { a.addEventListener('pointerenter', () => light(a.dataset.n, true)); a.addEventListener('pointerleave', () => light(a.dataset.n, false)); }
};

// the editor's top bar, its own markup: hover its buttons for their tooltips
FIGS.topbar = (box) => {
  const top = partSel('header.top', true), q = (id) => top.querySelector('#' + id);
  for (const id of ['tabs', 'job']) if (q(id)) q(id).remove();
  const d = q('dirty'); if (d) { d.hidden = false; d.textContent = '3 unsaved'; }
  for (const b of top.querySelectorAll('.modes button')) { b.setAttribute('aria-selected', String(b.dataset.mode === 'level')); setTip(b, b.dataset.mode === 'level' ? 'view.levels' : 'view.gauntlets'); }
  const prev = setTip(navButton('prev'), 'level.prev'), cur = setTip(navCurrent(), 'level.picker'), next = setTip(navButton('next'), 'level.next');
  if (q('lnav')) q('lnav').append(prev, cur, next);
  const tips = { undo: 'top.undo', redo: 'top.redo', revert: 'top.revert', save: 'top.save', dirty: 'top.unsaved', play: 'top.play', watch: 'top.watch', sim: 'top.sim', help: 'top.help' };
  for (const [id, key] of Object.entries(tips)) setTip(q(id), key, id === 'undo' ? 'Next: “Saw added at 92.1 m”.' : id === 'dirty' ? 'Unsaved: level 13, the gallery and the tomb.' : '');
  strip(top);
  for (const b of top.querySelectorAll('button')) b.tabIndex = -1;
  box.append(top, el('div', 'cap', 'The editor’s own top bar (it scrolls sideways here). Hover a button: the card that comes up is the editor’s tooltip.'));
};

// the level picker, open: every level by zone (the campaign as saved; level 13 open, being checked, unsaved)
FIGS.picker = (box) => {
  const p = el('div', 'picker open'), levels = CONTENT.levels.length ? CONTENT.levels : [];
  const rows = [];
  for (const lv of levels) {
    const z = K.zoneOf(lv.number);
    if (!rows.length || rows[rows.length - 1].zone !== z || rows[rows.length - 1].levels.length >= 10) rows.push({ zone: z, levels: [] });
    rows[rows.length - 1].levels.push(lv);
  }
  for (const zr of rows) {
    const row = el('div', `prow zone-${zr.zone}`, `<div class="pzone"><b>${esc(K.ZONES[zr.zone].short)}</b><span>${zr.levels[0].number}–${zr.levels[zr.levels.length - 1].number}</span></div>`);
    const tiles = el('div', 'ptiles');
    for (const lv of zr.levels) {
      const on = lv.number === 13, s = K.spans(lv.L)[0];
      const t = el('button', 'ptile' + (on ? ' on' : ''), `<span class="state ${on ? 'busy' : 'ok'}"></span><span class="pnum">${lv.number}</span>${on ? '<span class="unsaved"></span>' : ''}`);
      t.append(skyline(lv.L, zr.zone));
      if (s) t.append(el('span', 'pg', esc(titled(s.name))));
      t.tabIndex = -1;
      tiles.append(t);
    }
    row.append(tiles);
    p.append(row);
  }
  if (!levels.length) p.append(el('div', 'missing', 'The levels come from the editor’s server: open this wiki from the editor (its ? button).'));
  box.append(p, el('div', 'cap', 'Each level’s course in miniature (its gauntlet’s span in gold). Here level 13 is open (volt), being checked (the pulsing dot), with unsaved edits (the small dot).'));
};
function skyline(L, zone, w = 58, h = 16) { // (as the picker draws a level in miniature)
  const cv = el('canvas');
  cv.width = w * 2; cv.height = h * 2;
  const ctx = cv.getContext('2d');
  ctx.scale(2, 2);
  const n = Math.max(1, Math.ceil(L.length / U)), look = ZONE_LOOK[zone];
  const bins = new Array(w).fill(0), gold = new Array(w).fill(false);
  for (let c = 0; c < Math.min(n, L.cols.length); c++) { const b = Math.min(w - 1, Math.floor((c / n) * w)), k = L.cols[c]; if (k.g) gold[b] = true; else bins[b] = Math.max(bins[b], k.h); }
  const top = Math.max(8, ...bins);
  for (let b = 0; b < w; b++) {
    if (gold[b]) { ctx.fillStyle = 'rgba(255,194,58,0.85)'; ctx.fillRect(b, 1, 1, h - 1); continue; }
    const hh = (bins[b] / top) * (h - 2);
    ctx.fillStyle = look.accent; ctx.globalAlpha = 0.75; ctx.fillRect(b, h - hh, 1, hh); ctx.globalAlpha = 1;
  }
  return cv;
}

// the course canvases (data-scene: COURSE) and grid canvases (GRID_SCENES)
FIGS.course = (canvas) => {
  const scene = COURSE[canvas.dataset.scene]();
  courseFigure(canvas, scene);
  if (scene.summary) { const cap = canvas.closest('figure').querySelector('figcaption'); if (cap) cap.insertAdjacentHTML('beforeend', ` <span class="live">Its History heading says: “${esc(scene.summary)}”.</span>`); }
};
FIGS.grid = (canvas) => gridFigure(canvas, GRID_SCENES[canvas.dataset.scene]());

// a hazard's card, as the course view shows it at the pointer (levels.js hazardAt: the tip, then where)
FIGS.hazardcard = (box) => {
  const t = TIPS['hazard.cube-saw'], extra = `At 92.1 m, in a wall 3 high; a blade back to back with it. ${TIPS['hazard.saw'].text}`;
  box.append(el('div', 'tip in', `<div class="tip-head"><b>${esc(t.title)}</b></div><div class="tip-text">${esc(`${t.text} ${extra}`)}</div>`));
};

// The History card, working: a level as saved, edited with the editor's own course model, each edit in the words its
// describers write (describe.js). Click an edit to go back to it; hover one and its spot pulses on the course
const DEMO = (() => {
  const now = Date.now(), min = 60000, Ls = [RUNL];
  let t = H.timeline(K.levelText(RUNL, 13), 'As saved', now - 31 * min), L = RUNL;
  const edit = (next, kind, ago, hint = null) => {
    const d = D.describeLevel(kind, Ls[t.cursor], next, hint);
    Ls.length = t.cursor + 1; Ls.push(next);
    t = H.record(t, K.levelText(next, 13), d.label, d.focus, kind, now - ago * min);
    L = next;
  };
  let n = L; for (let c = 34; c <= 36; c++) n = K.setHeight(n, c, 2, 'crate'); edit(n, 'walls', 27);
  edit(K.addSaw(L, 15.6), 'saw-add', 22);
  t = H.markSaved(t);
  edit(K.duplicateCols(L, 9, 11), 'cols-in', 9, { c0: 12, c1: 14 });
  edit(K.setLength(L, 33), 'finish', 5);
  edit(K.addDisco(L, 18.6, 2.6), 'disco-add', 1);
  t = H.undo(t);
  return { t, Ls };
})();
FIGS.history = (box) => {
  let t = DEMO.t, focus = null;
  const card = el('section', 'card history-card'), fig = el('figure', 'figure-canvas'), cv = el('canvas');
  fig.append(cv, el('figcaption', null, 'The course at the edit the card’s at, with <b>Changes</b> on: what’s changed since the save. Hover an edit: its spot pulses here.'));
  box.append(card, fig);
  const scene = { zone: 'city', L: DEMO.Ls[t.cursor], cols: [0, 56], rows: 8, overlays: { changes: true }, diff: null, state: () => ({ focus }) };
  const render = () => {
    const was = DEMO.Ls[t.saved], now = DEMO.Ls[t.cursor];
    scene.L = now; scene.diff = D.diffCourse(was, now);
    renderHistory(card, { t, summary: D.summarizeCourse(scene.diff),
      onJump: (i) => { t = H.jump(t, i); render(); },
      onHover: (f) => { focus = f; },
      onShow: (f) => { focus = f; setTimeout(() => { if (focus === f) focus = null; }, 1600); } });
    setTip(card.querySelector('h3'), 'card.history');
  };
  render();
  courseFigure(cv, scene);
};

// the Revert dialog, as the editor draws it (app.js unsaved()): level 13 with the History card's edits, the gallery
FIGS.revert = (box) => {
  const t = DEMO.t, edits = H.sinceSaved(t), sum = D.summarizeCourse(D.diffCourse(DEMO.Ls[t.saved], DEMO.Ls[t.cursor]));
  const g2 = G.fillRect(GRID, 18, 4, 21, 4, '#'), gsum = D.summarizeGrid(D.diffGrid(GRID, g2));
  const gname = levelInfo(13).gauntlet || 'gallery';
  const stage = el('div', 'modal-stage');
  stage.innerHTML = `<div class="modal wide" role="dialog" aria-label="2 unsaved edits"><h2>2 unsaved edits</h2><div class="modal-body">
    <p>Revert takes the ticked ones back to what’s saved: Undo brings their edits back (a gauntlet never saved goes for good). Save saves every one, together.</p>
    <ul class="unsaved-list">
      <li class="level"><label><input type="checkbox" checked><b>Level 13</b><span>its course: ${esc(sum)}</span></label><button class="small ghost" tabindex="-1">Show</button>
        <details class="h-edits" open><summary>${edits.length} edit${edits.length === 1 ? '' : 's'} since the save</summary><ol>${edits.map((e) => `<li><span>${esc(e.label)}</span><i>${esc(H.ago(e.at))}</i></li>`).join('')}</ol></details></li>
      <li class="gauntlet"><label><input type="checkbox"><b>The ${esc(titled(gname))}</b><span>its grid: ${esc(gsum)}</span></label><button class="small ghost" tabindex="-1">Show</button></li>
    </ul></div>
    <div class="modal-actions"><button class="ghost" tabindex="-1">Cancel</button><button class="danger" tabindex="-1">Revert</button><button class="primary" tabindex="-1">Save all</button></div></div>`;
  const ticks = [...stage.querySelectorAll('input')], revert = stage.querySelector('.danger');
  const update = () => { const n = ticks.filter((b) => b.checked).length; revert.textContent = n ? `Revert ${n === ticks.length && n > 1 ? 'all ' : ''}${n}` : 'Revert'; revert.disabled = !n; };
  for (const b of ticks) b.addEventListener('change', update);
  update();
  box.append(stage, el('div', 'cap', 'Tick or untick them: this one works like the editor’s, but reverts nothing.'));
};

// the gauntlet tabs and the New gauntlet dialog; what a new one starts as
FIGS.newgauntlet = (box) => {
  const laid = new Map();
  for (const lv of CONTENT.levels) for (const s of K.spans(lv.L)) laid.set(s.name, lv.number);
  const names = CONTENT.gauntlets.map((g) => g.name).sort((a, b) => (laid.get(a) || 999) - (laid.get(b) || 999));
  const tabs = el('nav', 'tabs figure-tabs');
  for (const name of (names.length ? names : ['tomb', 'gallery'])) tabs.insertAdjacentHTML('beforeend', `<button class="tab" aria-selected="${name === 'gallery'}" tabindex="-1"><span class="state ok"></span><span class="name">${esc(titled(name))}</span><span class="lvl">LV ${laid.get(name) || '?'}</span></button>`);
  tabs.insertAdjacentHTML('beforeend', '<button class="tab" tabindex="-1"><span class="state busy"></span><span class="name">Torii</span><span class="lvl none">NO LEVEL</span><span class="unsaved"></span></button>');
  const add = el('button', 'tab add', `${svg('<path d="M12 5v14M5 12h14"/>')}<span>New gauntlet</span>`);
  add.tabIndex = -1;
  setTip(add, 'prop.new', 'In no level till a level lays it (Levels → the level → Gauntlet).');
  tabs.append(add);
  const taken = new Set(names);
  const why = (v) => (!v ? 'A name, to make it' : !/^[a-z][a-z0-9_]{0,31}$/.test(v) ? 'A letter first, then lower case letters, digits or _ (up to 32)' : taken.has(v) ? `There’s a gauntlet called ${v} already` : '');
  const stage = el('div', 'modal-stage');
  stage.innerHTML = `<div class="modal" role="dialog" aria-label="A new gauntlet"><h2>A new gauntlet</h2><div class="modal-body"><div>
    <p>Its name: lower case letters, digits and _ (it names its file, assets/gauntlets/&lt;name&gt;.txt). It starts as a plain room to design from, in no level till one lays it.</p>
    <input class="modal-input" value="torii" spellcheck="false" autocomplete="off" aria-label="Its name"><div class="modal-why"></div></div></div>
    <div class="modal-actions"><button class="ghost" tabindex="-1">Cancel</button><button class="primary" tabindex="-1">Make it</button></div></div>`;
  const input = stage.querySelector('input'), msg = stage.querySelector('.modal-why'), ok = stage.querySelector('.primary');
  input.addEventListener('input', () => { const w = why(input.value.trim()); msg.textContent = w; ok.disabled = !!w; });
  const room = el('figure', 'figure-canvas'), cv = el('canvas');
  room.append(cv, el('figcaption', null, 'What a new gauntlet starts as (grid.js template): a plain room, its doors a row up off its floor, one way through. Design it from there.'));
  box.append(tabs, stage, room);
  gridFigure(cv, { overlays: { grid: true } }, null, G.template());
};

// the keyboard
const KEYS = {
  both: [[['⌘S'], 'Save every unsaved level and grid, together'], [['⌘Z'], 'Undo'], [['⇧⌘Z', '⌘Y'], 'Redo'], [['⇧G'], 'From a level to its gauntlet’s grid, and back'],
    [['Scroll'], 'Look along the course, or around the grid'], [['⌘', 'Scroll'], 'Zoom at the pointer (a pinch too)'], [['Space', 'drag'], 'Pan'], [['[', ']'], 'Zoom out, in'],
    [['F'], 'Fit (in the Gauntlets view, with a selection: flip it)'], [['P'], 'Play or pause the bot’s run'], [[',', '.'], 'Step the replay back, on'], [['Home', 'End'], 'The run’s start, its end'],
    [['T'], 'The Start tool: where Play starts'], [['Esc'], 'Let go of a selection or a preview; close the picker or a dialog']],
  level: [[['W', 'K', 'B', 'S', 'D'], 'Walls, Kind, Barrier, Saw, Disco ball'], [['C', 'V', 'G', 'T'], 'Columns, Select, Gauntlet, Start'], [['1', '2', '3'], 'Crates, hay, hay-topped crates'],
    [['⌥', 'click'], 'Take away: down to the street, barriers off, a saw or disco ball gone, columns out (a right click too)'], [['⇧', 'drag'], 'Walls: as high as where you started'],
    [['⌘C', '⌘X', '⌘V'], 'Copy, cut, put in the selected columns'], [['⌫'], 'Take out the selected columns'], [['L'], 'Every level (the picker)'], [['⌥←', '⌥→'], 'The level before, after'],
    [['Double-click'], 'The gauntlet: edit its grid']],
  gauntlet: [[['B', 'R', 'V', 'C', 'E', 'I', 'T'], 'Brush, Rectangle, Select, Columns, Rows, Pick, Start'], [['1', '–', '0'], 'Cells: air, stone, cube, bumper, gates, saws, the doors'],
    [['Right', 'click'], 'Paint with the second cell'], [['⌥', 'click'], 'Brush: pick up the cell there'], [['X'], 'Swap the first and second cells'],
    [['⌘C', '⌘X', '⌘V'], 'Copy, cut, paste a selection'], [['F'], 'Flip the selection'], [['⌫'], 'Fill the selection with the second cell']],
};
FIGS.keys = (box) => {
  const words = new Set(['click', 'drag', 'Scroll', 'Double-click', 'Right', '–']); // (the rest are keys: key caps)
  box.innerHTML = `<div class="keys-table">${KEYS[box.dataset.set].map(([ks, d]) => `<div class="k">${ks.map((k) => (words.has(k) ? `<span>${esc(k)}</span>` : `<kbd class="keycap">${esc(k)}</kbd>`)).join('')}</div><div class="d">${esc(d)}</div>`).join('')}</div>`;
};

// how it's built: the browser, its server, the checkers, the game
FIGS.arch = (box) => {
  const B = (x, y, w, h, t, s, hot = false) => `<g class="box${hot ? ' hot' : ''}"><rect x="${x}" y="${y}" width="${w}" height="${h}" rx="10"/><text class="t" x="${x + 14}" y="${y + 22}">${t}</text><text class="s" x="${x + 14}" y="${y + 39}">${s}</text></g>`;
  const wire = (d, id) => `<path id="${id}" class="wire" d="${d}"/><circle r="3.2" fill="#29e7ff"><animateMotion dur="2.4s" repeatCount="indefinite"><mpath href="#${id}"/></animateMotion></circle>`;
  box.innerHTML = `<svg viewBox="0 0 960 372" role="img" aria-label="The browser runs the editor; its server runs the checkers and the game, and writes the files">
    <text class="head" x="20" y="20" fill="#29e7ff">IN THE BROWSER</text><text class="head" x="372" y="20" fill="#ff2bd6">ITS SERVER</text><text class="head" x="664" y="20" fill="#ffc23a">THE GAME’S CODE, ITS FILES</text>
    ${B(20, 34, 290, 52, 'app.js', 'the shell: the views, top bar, Save, Revert, keys', true)}
    ${B(20, 98, 290, 52, 'levels.js · gauntlets.js', 'the two views: tools, inspector, replay')}
    ${B(20, 162, 290, 52, 'course.js · grid.js', 'the models: every edit (no DOM: tested in Node)')}
    ${B(20, 226, 290, 52, 'courseview.js · view.js', 'the canvases: a course, a grid')}
    ${B(20, 290, 290, 52, 'history · describe · tips · tools', 'the History, its words; tooltips; tool lists')}
    ${B(372, 120, 230, 132, 'server.py', '127.0.0.1:8765 · Python, no packages', true)}
    <text class="s" x="386" y="186">GET /api/levels, /api/gauntlets</text><text class="s" x="386" y="202">POST /api/levels/check, /api/check</text><text class="s" x="386" y="218">POST /api/save, /api/play</text><text class="s" x="386" y="234">POST /api/simulator · GET /wiki/</text>
    ${B(664, 34, 276, 52, 'build/level_check · gauntlet_check', 'the game’s own code judging a level, a grid')}
    ${B(664, 126, 276, 52, 'build/cuckoo_stack', 'the desktop game: Play, Watch the bot')}
    ${B(664, 218, 276, 52, 'assets/levels · assets/gauntlets', 'what Save writes, and the layouts (.inc)')}
    ${B(664, 300, 276, 52, 'the iOS Simulator', 'Simulator: the app built and installed')}
    ${wire('M310 186 C 340 186, 340 186, 372 186', 'w1')}${wire('M602 150 C 630 150, 630 60, 664 60', 'w2')}${wire('M602 180 C 632 180, 632 152, 664 152', 'w3')}
    ${wire('M602 210 C 632 210, 632 244, 664 244', 'w4')}${wire('M602 240 C 630 240, 630 326, 664 326', 'w5')}
    <text class="lbl" x="314" y="178">JSON</text><text class="lbl" x="612" y="96">runs</text><text class="lbl" x="612" y="166">opens</text><text class="lbl" x="612" y="234">writes</text><text class="lbl" x="612" y="300">builds</text>
  </svg>`;
};

// ---- the page: contents (the section you're reading lit), search
function contents() {
  const links = $$('#toc a'), targets = links.map((a) => document.getElementById(decodeURIComponent(a.hash.slice(1))));
  let ticking = false;
  const spy = () => {
    ticking = false;
    let at = -1;
    targets.forEach((t, i) => { if (t && !t.closest('[hidden]') && t.getBoundingClientRect().top < 140) at = i; });
    links.forEach((a, i) => a.classList.toggle('on', i === at));
  };
  addEventListener('scroll', () => { if (!ticking) { ticking = true; requestAnimationFrame(spy); } }, { passive: true });
  spy();
}
function search() {
  const input = document.getElementById('search'), secs = $$('.wsec'), count = el('span', 'count');
  input.parentElement.append(count);
  const run = () => {
    const q = input.value.trim().toLowerCase();
    let n = 0;
    for (const s of secs) {
      const hit = !q || s.textContent.toLowerCase().includes(q);
      s.hidden = !hit;
      if (hit && q) n++;
      for (const r of s.querySelectorAll('.tipref, .wcard')) r.classList.toggle('match', !!q && r.textContent.toLowerCase().includes(q));
    }
    for (const a of $$('#toc a')) { const t = document.getElementById(decodeURIComponent(a.hash.slice(1))), sec = t && t.closest('.wsec'); a.classList.toggle('gone', !!q && !!sec && sec.hidden); }
    count.textContent = q ? (n ? `${n} section${n === 1 ? '' : 's'}` : 'nothing') : '';
  };
  input.addEventListener('input', run);
  input.addEventListener('keydown', (e) => {
    if (e.key === 'Enter') { const first = document.querySelector('.tipref.match, .wcard.match') || secs.find((s) => !s.hidden); if (first) first.scrollIntoView({ block: 'start' }); }
    if (e.key === 'Escape') { input.value = ''; run(); input.blur(); }
  });
  addEventListener('keydown', (e) => { if (e.key === '/' && !e.target.closest('input, textarea')) { e.preventDefault(); input.focus(); } });
}

async function main() {
  initTooltips();
  const [page, levels, grids] = await Promise.all([
    fetch('/').then((r) => r.text()).then((t) => new DOMParser().parseFromString(t, 'text/html')).catch(() => null),
    fetch('/api/levels').then((r) => r.json()).catch(() => null),
    fetch('/api/gauntlets').then((r) => r.json()).catch(() => null),
  ]);
  if (page) EDITOR = page;
  CONTENT = { levels: levels ? levels.levels.map((l) => ({ number: l.number, L: K.parseLevel(l.text) })) : [], gauntlets: grids ? grids.gauntlets : [] };
  for (const r of $$('.tipref')) tipref(r);
  for (const f of $$('[data-fig]')) {
    try { FIGS[f.dataset.fig](f); } catch (e) { console.error(f.dataset.fig, e); f.append(el('div', 'missing', `This picture didn’t draw: ${esc(e.message)}`)); }
  }
  contents();
  search();
  kick();
  if (location.hash) { const t = document.getElementById(decodeURIComponent(location.hash.slice(1))); if (t) requestAnimationFrame(() => t.scrollIntoView({ block: 'start' })); }
}
main();
