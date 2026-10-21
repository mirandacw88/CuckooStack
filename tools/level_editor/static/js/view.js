// The level editor's canvas: a gauntlet drawn as the game builds it (stone in gold, cyber cubes in cyan, bumpers and
// gates in pink, red-hot blades in the floors), and over it what the checker found: where the hen's feet can get (a
// way through, or a dead end), the bot's way, its near misses, where it crashed; the cells changed since the last
// save; the selection; and the hen herself in the replay, her eggs under her. Plus the minimap: the whole gauntlet.
import { cols, DOORS } from './grid.js';

export const RULER_TOP = 20, RULER_LEFT = 30;

export const C = {
  bg: '#07040d', air: '#0e0919', line: 'rgba(255,255,255,0.035)', major: 'rgba(255,255,255,0.075)',
  stone: '#22170b', stoneLit: '#2f2010', stoneEdge: '#e2a43c',
  cube: '#05202a', cubeEdge: '#29e7ff', cubeInner: 'rgba(41,231,255,0.22)',
  bumper: '#3a0731', bumperEdge: '#ff2bd6',
  gate: '#170828', gateEdge: '#b45cff', gateArrow: '#ff8ae9',
  blade: '#ff4426', bladeHot: '#ffcf9a',
  entry: '#3dff8a', exit: '#f4ff5a',
  way: 'rgba(61,255,138,0.12)', dead: 'rgba(255,43,74,0.32)',
  pathR: '#29e7ff', pathL: '#ff5ad9', egg: '#fff3dc', eggRim: '#29e7ff',
  perfect: '#ff2bd6', shave: '#ff4426', call: '#29e7ff', crash: '#ff2b4a', lint: '#ffb43a', changed: '#ffffff',
  ruler: '#0b0714', rulerText: 'rgba(233,228,255,0.55)', rulerHot: '#f4ff5a', select: '#f4ff5a',
};
export const KIND = [{ label: 'PERFECT', short: 'P', color: C.perfect }, { label: 'CLOSE SHAVE', short: 'S', color: C.shave },
  { label: 'CLOSE CALL', short: 'C', color: C.call }];

const solidish = (ch) => ch === '#' || ch === 'S';
const cubeish = (ch) => ch === 'H' || ch === 'C';

export class GridView {
  constructor(canvas, minimap) {
    this.canvas = canvas;
    this.ctx = canvas.getContext('2d');
    this.mini = minimap;
    this.mctx = minimap.getContext('2d');
    this.z = 18; // a cell, CSS px
    this.ox = RULER_LEFT + 12; // cell (0, 0)'s top left, CSS px
    this.oy = RULER_TOP + 12;
    this.w = 1; this.h = 1;
    this.rows = [];
  }

  resize() {
    const dpr = window.devicePixelRatio || 1;
    for (const [cv, ctx] of [[this.canvas, this.ctx], [this.mini, this.mctx]]) {
      const r = cv.getBoundingClientRect();
      cv.width = Math.max(1, Math.round(r.width * dpr));
      cv.height = Math.max(1, Math.round(r.height * dpr));
      ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    }
    const r = this.canvas.getBoundingClientRect();
    this.w = r.width; this.h = r.height;
  }

  // ---- the view: zoom, pan, where a point is
  cellAt(px, py) { return { c: Math.floor((px - this.ox) / this.z), i: Math.floor((py - this.oy) / this.z) }; }
  inGrid(c, i) { return i >= 0 && i < this.rows.length && c >= 0 && c < cols(this.rows); }
  fit(rows = this.rows) {
    const h = this.h - RULER_TOP - 28;
    this.z = Math.max(6, Math.min(40, Math.floor(h / Math.max(1, rows.length))));
    this.ox = RULER_LEFT + 12;
    this.oy = RULER_TOP + Math.max(10, (this.h - RULER_TOP - rows.length * this.z) / 2);
  }
  zoomAt(px, py, factor) {
    const z = Math.max(4, Math.min(64, this.z * factor));
    this.ox = px - ((px - this.ox) * z) / this.z;
    this.oy = py - ((py - this.oy) * z) / this.z;
    this.z = z;
    this.clamp();
  }
  panBy(dx, dy) { this.ox += dx; this.oy += dy; this.clamp(); }
  centerOn(c, i) {
    this.ox = (RULER_LEFT + this.w) / 2 - (c + 0.5) * this.z;
    this.oy = (RULER_TOP + this.h) / 2 - (i + 0.5) * this.z;
    this.clamp();
  }
  clamp() { // (some of the grid always on screen)
    const gw = cols(this.rows) * this.z, gh = this.rows.length * this.z;
    this.ox = Math.min(this.w - 80, Math.max(RULER_LEFT + 80 - gw, this.ox));
    this.oy = Math.min(this.h - 60, Math.max(RULER_TOP + 60 - gh, this.oy));
  }
  visible() { // (the cells on screen)
    return { c0: Math.max(0, Math.floor((RULER_LEFT - this.ox) / this.z)), c1: Math.min(cols(this.rows) - 1, Math.ceil((this.w - this.ox) / this.z)),
      i0: Math.max(0, Math.floor((RULER_TOP - this.oy) / this.z)), i1: Math.min(this.rows.length - 1, Math.ceil((this.h - this.oy) / this.z)) };
  }

  // ---- drawing
  draw(s) {
    const { ctx, z } = this;
    const rows = (this.rows = s.rows);
    const n = cols(rows);
    ctx.fillStyle = C.bg;
    ctx.fillRect(0, 0, this.w, this.h);
    if (!n) return;
    const v = this.visible();
    const X = (c) => this.ox + c * z, Y = (i) => this.oy + i * z;
    // air, and the grid's faint lines
    ctx.fillStyle = C.air;
    ctx.fillRect(X(v.c0), Y(v.i0), (v.c1 - v.c0 + 1) * z, (v.i1 - v.i0 + 1) * z);
    if (s.overlays.grid && z >= 7) {
      ctx.lineWidth = 1;
      for (let c = v.c0; c <= v.c1 + 1; c++) { ctx.strokeStyle = c % 10 === 0 ? C.major : C.line; line(ctx, X(c) + 0.5, Y(v.i0), X(c) + 0.5, Y(v.i1 + 1)); }
      for (let i = v.i0; i <= v.i1 + 1; i++) { ctx.strokeStyle = (rows.length - i) % 10 === 0 ? C.major : C.line; line(ctx, X(v.c0), Y(i) + 0.5, X(v.c1 + 1), Y(i) + 0.5); }
    }
    // what the checker says, dimmed while it looks at the latest edit (gone if the grid's size changed since)
    const said = s.check && s.check.ok && (s.fresh || s.stale);
    const dim = s.fresh ? 1 : 0.35;
    // where her feet can get: on a way through (green), a dead end (red)
    const map = said ? s.check.cellMap : null;
    ctx.globalAlpha = dim;
    if (s.overlays.reach && map) {
      for (let i = v.i0; i <= v.i1; i++)
        for (let c = v.c0; c <= v.c1; c++) {
          const k = map[i] && map[i][c];
          if (k === 'w' || k === 'd') { ctx.fillStyle = k === 'w' ? C.way : C.dead; ctx.fillRect(X(c), Y(i), z, z); }
        }
    }
    ctx.globalAlpha = 1;
    // the cells
    for (let i = v.i0; i <= v.i1; i++)
      for (let c = v.c0; c <= v.c1; c++) this.cell(rows[i][c], X(c), Y(i), z);
    // stone and cubes edged where they meet anything else (rooms read as carved out)
    ctx.lineWidth = Math.max(1, Math.min(2.5, z / 9));
    for (let i = v.i0; i <= v.i1; i++)
      for (let c = v.c0; c <= v.c1; c++) {
        const ch = rows[i][c];
        const kind = solidish(ch) ? solidish : cubeish(ch) ? cubeish : null;
        if (!kind) continue;
        ctx.strokeStyle = kind === solidish ? C.stoneEdge : C.cubeEdge;
        const x = X(c), y = Y(i), o = ctx.lineWidth / 2;
        const other = (cc, ii) => !kind(cc >= 0 && cc < n && ii >= 0 && ii < rows.length ? rows[ii][cc] : ch);
        ctx.beginPath();
        if (other(c, i - 1)) { ctx.moveTo(x, y + o); ctx.lineTo(x + z, y + o); }
        if (other(c, i + 1)) { ctx.moveTo(x, y + z - o); ctx.lineTo(x + z, y + z - o); }
        if (other(c - 1, i)) { ctx.moveTo(x + o, y); ctx.lineTo(x + o, y + z); }
        if (other(c + 1, i)) { ctx.moveTo(x + z - o, y); ctx.lineTo(x + z - o, y + z); }
        ctx.stroke();
      }
    // blades: half out of the floor, into the cell over it
    for (let i = v.i0; i <= v.i1; i++)
      for (let c = v.c0; c <= v.c1; c++) if (rows[i][c] === 'S' || rows[i][c] === 'C') this.blade(X(c) + z / 2, Y(i), z);
    // changed since the last save: outlined, a corner marked
    if (s.overlays.changes && s.changed) {
      ctx.save();
      ctx.lineWidth = Math.max(1.5, Math.min(2.5, z / 8));
      for (const key of s.changed) {
        const [c, i] = key.split(',').map(Number);
        if (c < v.c0 || c > v.c1 || i < v.i0 || i > v.i1) continue;
        ctx.strokeStyle = 'rgba(244,255,90,0.85)'; ctx.strokeRect(X(c) + 1, Y(i) + 1, z - 2, z - 2);
        ctx.fillStyle = C.changed;
        ctx.beginPath(); ctx.moveTo(X(c + 1) - Math.max(4, z * 0.32), Y(i)); ctx.lineTo(X(c + 1), Y(i)); ctx.lineTo(X(c + 1), Y(i) + Math.max(4, z * 0.32)); ctx.fill();
      }
      ctx.restore();
    }
    const run = said ? s.run : null; // (the bot's run being shown, at the replay's rate)
    ctx.globalAlpha = dim;
    if (s.overlays.path && run) this.path(run.steps, rows.length);
    if (s.overlays.misses && run) {
      for (const [c, b, kind] of run.nearMisses) this.badge(X(c) + z / 2, Y(rows.length - 1 - b) + z * 0.32, KIND[kind]); // (up in its cell: a blade under it shows)
      if (run.crash) this.crash(X(run.crash[0]) + z / 2, Y(rows.length - 1 - (run.crash[1] + run.crash[2])) + z / 2, 'out of eggs');
    }
    ctx.globalAlpha = 1;
    if (s.game && s.game.crash) this.crash(X(s.game.crash[0]), Y(rows.length - 1 - Math.round(s.game.crash[1])) + z / 2, 'the game\u2019s bot');
    if (s.overlays.misses) for (const l of s.lints) this.lint(X(l.c) + z / 2, Y(l.i) + z / 2);
    // previews (a rectangle, columns or rows about to be copied or taken out), the selection, the hover
    if (s.preview) this.preview(s.preview, X, Y);
    if (s.selection) this.selection(s.selection, X, Y);
    if (s.hover && this.inGrid(s.hover.c, s.hover.i) && !s.preview) {
      ctx.strokeStyle = 'rgba(255,255,255,0.85)'; ctx.lineWidth = 1.5;
      ctx.strokeRect(X(s.hover.c) + 0.75, Y(s.hover.i) + 0.75, z - 1.5, z - 1.5);
    }
    if (s.focus) { // (a History entry hovered: where its edit was)
      const f = s.focus, pulse = 0.5 + 0.5 * Math.sin(performance.now() / 140);
      ctx.save();
      ctx.fillStyle = `rgba(244,255,90,${(0.1 + 0.1 * pulse).toFixed(3)})`; ctx.fillRect(X(f.c0), Y(f.i0), (f.c1 - f.c0 + 1) * z, (f.i1 - f.i0 + 1) * z);
      ctx.strokeStyle = `rgba(244,255,90,${(0.6 + 0.4 * pulse).toFixed(3)})`; ctx.lineWidth = 2.5; ctx.strokeRect(X(f.c0) + 1, Y(f.i0) + 1, (f.c1 - f.c0 + 1) * z - 2, (f.i1 - f.i0 + 1) * z - 2);
      ctx.restore();
    }
    if (s.start) this.startHen(s.start.c, s.start.b, s.start.dir, rows.length, s.hoverStart);
    if (s.replay && run && s.fresh) this.hen(run.steps, s.replay.step, rows.length);
    this.rulers(rows, s.hover);
  }

  cell(ch, x, y, z) {
    const { ctx } = this;
    switch (ch) {
      case '#': case 'S':
        ctx.fillStyle = C.stone; ctx.fillRect(x, y, z, z);
        if (z >= 10) { ctx.fillStyle = C.stoneLit; ctx.fillRect(x + z * 0.12, y + z * 0.12, z * 0.76, z * 0.76); }
        break;
      case 'H': case 'C':
        ctx.fillStyle = C.cube; ctx.fillRect(x, y, z, z);
        if (z >= 10) { ctx.strokeStyle = C.cubeInner; ctx.lineWidth = 1; ctx.strokeRect(x + z * 0.22 + 0.5, y + z * 0.22 + 0.5, z * 0.56, z * 0.56); }
        break;
      case 'B': {
        ctx.fillStyle = C.bumper; ctx.fillRect(x, y, z, z);
        ctx.strokeStyle = C.bumperEdge; ctx.lineWidth = Math.max(1, z / 10); ctx.strokeRect(x + 1, y + 1, z - 2, z - 2);
        if (z >= 9) {
          ctx.fillStyle = C.bumperEdge; ctx.beginPath();
          ctx.moveTo(x + z / 2, y + z * 0.25); ctx.lineTo(x + z * 0.75, y + z / 2); ctx.lineTo(x + z / 2, y + z * 0.75); ctx.lineTo(x + z * 0.25, y + z / 2); ctx.fill();
        }
        break;
      }
      case 'R': case 'L': {
        ctx.fillStyle = C.gate; ctx.fillRect(x, y, z, z);
        ctx.strokeStyle = C.gateEdge; ctx.lineWidth = Math.max(1, z / 12); ctx.strokeRect(x + 1, y + 1, z - 2, z - 2);
        const d = ch === 'R' ? 1 : -1, cx = x + z / 2;
        ctx.strokeStyle = C.gateArrow; ctx.lineWidth = Math.max(1.2, z / 7); ctx.lineCap = 'round'; ctx.lineJoin = 'round';
        ctx.beginPath(); ctx.moveTo(cx - d * z * 0.14, y + z * 0.25); ctx.lineTo(cx + d * z * 0.14, y + z / 2); ctx.lineTo(cx - d * z * 0.14, y + z * 0.75); ctx.stroke();
        ctx.lineCap = 'butt';
        break;
      }
      case '>': case 'X': {
        const col = ch === '>' ? C.entry : C.exit;
        ctx.fillStyle = col + '30'; ctx.fillRect(x, y, z, z);
        ctx.fillStyle = col; ctx.beginPath();
        ctx.moveTo(x + z * 0.28, y + z * 0.22); ctx.lineTo(x + z * 0.78, y + z / 2); ctx.lineTo(x + z * 0.28, y + z * 0.78); ctx.fill();
        break;
      }
      default: break;
    }
  }

  blade(cx, cy, z) {
    const { ctx } = this;
    const r = z * 0.42;
    ctx.save();
    ctx.shadowColor = C.blade; ctx.shadowBlur = Math.min(12, z * 0.6);
    ctx.fillStyle = C.blade;
    ctx.beginPath(); ctx.arc(cx, cy, r, Math.PI, 0); ctx.closePath(); ctx.fill();
    ctx.restore();
    if (z >= 12) { // teeth, and the hub
      ctx.fillStyle = C.blade;
      for (let k = 1; k < 8; k++) {
        const a = Math.PI + (k * Math.PI) / 8, t = r * 1.22;
        ctx.beginPath(); ctx.moveTo(cx + Math.cos(a - 0.12) * r, cy + Math.sin(a - 0.12) * r);
        ctx.lineTo(cx + Math.cos(a) * t, cy + Math.sin(a) * t); ctx.lineTo(cx + Math.cos(a + 0.12) * r, cy + Math.sin(a + 0.12) * r); ctx.fill();
      }
      ctx.fillStyle = C.bladeHot; ctx.beginPath(); ctx.arc(cx, cy, r * 0.32, Math.PI, 0); ctx.fill();
    }
  }

  path(steps, nrows) {
    const { ctx, z } = this;
    if (!steps || !steps.length) return;
    ctx.lineWidth = Math.max(1.5, z / 7); ctx.lineJoin = 'round'; ctx.lineCap = 'round';
    let prev = null;
    for (const st of steps) {
      const [c, b, n, d] = st;
      const p = { x: this.ox + (c + 0.5) * this.z, y: this.oy + (nrows - 1 - (b + n) + 0.5) * this.z, d };
      if (prev) {
        ctx.strokeStyle = prev.d > 0 ? C.pathR : C.pathL; ctx.globalAlpha = 0.75;
        ctx.beginPath(); ctx.moveTo(prev.x, prev.y); ctx.lineTo(p.x, p.y); ctx.stroke();
      }
      if (st[4] && z >= 8) { ctx.globalAlpha = 0.95; ctx.fillStyle = C.egg; ctx.beginPath(); ctx.arc(p.x, p.y, Math.max(1.6, z / 9), 0, Math.PI * 2); ctx.fill(); }
      prev = p;
    }
    ctx.globalAlpha = 1;
  }

  badge(x, y, kind) {
    const { ctx, z } = this;
    const r = Math.max(4, z * 0.3);
    ctx.save(); ctx.shadowColor = kind.color; ctx.shadowBlur = 8;
    ctx.fillStyle = kind.color; ctx.beginPath(); ctx.arc(x, y, r, 0, Math.PI * 2); ctx.fill(); ctx.restore();
    ctx.strokeStyle = '#fff'; ctx.lineWidth = 1.2; ctx.beginPath(); ctx.arc(x, y, r, 0, Math.PI * 2); ctx.stroke();
    if (z >= 13) { ctx.fillStyle = '#fff'; ctx.font = `700 ${Math.round(r * 1.25)}px "Chakra Petch", sans-serif`; ctx.textAlign = 'center'; ctx.textBaseline = 'middle'; ctx.fillText(kind.short, x, y + 0.5); }
  }

  crash(x, y, label) {
    const { ctx, z } = this;
    const r = Math.max(5, z * 0.42);
    ctx.save(); ctx.shadowColor = C.crash; ctx.shadowBlur = 12;
    ctx.strokeStyle = C.crash; ctx.lineWidth = Math.max(2, z / 6); ctx.lineCap = 'round';
    ctx.beginPath(); ctx.moveTo(x - r, y - r); ctx.lineTo(x + r, y + r); ctx.moveTo(x + r, y - r); ctx.lineTo(x - r, y + r); ctx.stroke();
    ctx.restore();
    if (label && z >= 10) { ctx.fillStyle = C.crash; ctx.font = '700 11px "Chakra Petch", sans-serif'; ctx.textAlign = 'left'; ctx.textBaseline = 'bottom'; ctx.fillText(label, x + r + 3, y - r); }
  }

  lint(x, y) {
    const { ctx, z } = this;
    const r = Math.max(4, z * 0.3);
    ctx.fillStyle = C.lint; ctx.beginPath(); ctx.moveTo(x, y - r); ctx.lineTo(x + r, y + r * 0.8); ctx.lineTo(x - r, y + r * 0.8); ctx.fill();
    if (z >= 14) { ctx.fillStyle = '#1a0d00'; ctx.font = `800 ${Math.round(r)}px sans-serif`; ctx.textAlign = 'center'; ctx.textBaseline = 'middle'; ctx.fillText('!', x, y + r * 0.2); }
  }

  preview(p, X, Y) {
    const { ctx, z } = this;
    const n = cols(this.rows);
    ctx.save();
    if (p.kind === 'rect') {
      const [ca, cb] = [Math.min(p.c0, p.c1), Math.max(p.c0, p.c1)], [ia, ib] = [Math.min(p.i0, p.i1), Math.max(p.i0, p.i1)];
      ctx.globalAlpha = 0.55;
      for (let i = ia; i <= ib; i++) for (let c = ca; c <= cb; c++) if (this.inGrid(c, i) && !DOORS.has(this.rows[i][c])) this.cell(p.ch === '.' ? null : p.ch, X(c), Y(i), z);
      if (p.ch === '.') { ctx.fillStyle = C.air; ctx.fillRect(X(ca), Y(ia), (cb - ca + 1) * z, (ib - ia + 1) * z); }
      ctx.globalAlpha = 1; ctx.strokeStyle = C.select; ctx.setLineDash([5, 4]); ctx.lineWidth = 1.5;
      ctx.strokeRect(X(ca) + 0.5, Y(ia) + 0.5, (cb - ca + 1) * z - 1, (ib - ia + 1) * z - 1);
    } else if (p.kind === 'cols') {
      const [a, b] = [Math.max(1, Math.min(p.c0, p.c1)), Math.min(n - 2, Math.max(p.c0, p.c1))];
      ctx.fillStyle = p.remove ? 'rgba(255,43,74,0.28)' : 'rgba(244,255,90,0.18)';
      ctx.fillRect(X(a), Y(0), (b - a + 1) * z, this.rows.length * z);
      ctx.strokeStyle = p.remove ? C.crash : C.select; ctx.lineWidth = 2; ctx.setLineDash(p.remove ? [4, 3] : []);
      ctx.strokeRect(X(a) + 1, Y(0) + 1, (b - a + 1) * z - 2, this.rows.length * z - 2);
      if (!p.remove) { ctx.setLineDash([]); ctx.strokeStyle = C.select; ctx.lineWidth = 3; line(ctx, X(b + 1), Y(0) - 6, X(b + 1), Y(this.rows.length) + 6); }
    } else if (p.kind === 'rows') {
      const [a, b] = [Math.min(p.i0, p.i1), Math.max(p.i0, p.i1)];
      ctx.fillStyle = p.remove ? 'rgba(255,43,74,0.28)' : 'rgba(244,255,90,0.18)';
      ctx.fillRect(X(0), Y(a), n * z, (b - a + 1) * z);
      ctx.strokeStyle = p.remove ? C.crash : C.select; ctx.lineWidth = 2; ctx.setLineDash(p.remove ? [4, 3] : []);
      ctx.strokeRect(X(0) + 1, Y(a) + 1, n * z - 2, (b - a + 1) * z - 2);
      if (!p.remove) { ctx.setLineDash([]); ctx.strokeStyle = C.select; ctx.lineWidth = 3; line(ctx, X(0) - 6, Y(a), X(n) + 6, Y(a)); }
    }
    ctx.restore();
  }

  selection(sel, X, Y) {
    const { ctx, z } = this;
    const [ca, cb] = [Math.min(sel.c0, sel.c1), Math.max(sel.c0, sel.c1)], [ia, ib] = [Math.min(sel.i0, sel.i1), Math.max(sel.i0, sel.i1)];
    let x = X(ca), y = Y(ia);
    const w = (cb - ca + 1) * z, h = (ib - ia + 1) * z;
    if (sel.float) { // the lifted block, where it'd land
      x = X(sel.float.c); y = Y(sel.float.i);
      ctx.save(); ctx.globalAlpha = 0.85;
      sel.float.block.forEach((line, yy) => line.forEach((ch, xx) => { if (ch !== null && ch !== '.') this.cell(ch, x + xx * z, y + yy * z, z); }));
      sel.float.block.forEach((line, yy) => line.forEach((ch, xx) => { if (ch === 'S' || ch === 'C') this.blade(x + xx * z + z / 2, y + yy * z, z); }));
      ctx.restore();
    }
    ctx.save();
    ctx.strokeStyle = '#000'; ctx.lineWidth = 2; ctx.strokeRect(x + 0.5, y + 0.5, w - 1, h - 1);
    ctx.strokeStyle = C.select; ctx.setLineDash([6, 4]); ctx.lineDashOffset = -(performance.now() / 60) % 10; ctx.lineWidth = 2;
    ctx.strokeRect(x + 0.5, y + 0.5, w - 1, h - 1);
    ctx.restore();
  }

  hen(steps, k, nrows) {
    const { ctx, z } = this;
    if (!steps.length) return;
    const st = steps[Math.max(0, Math.min(steps.length - 1, k))];
    const [c, b, n, d] = st;
    const x = this.ox + (c + 0.5) * z, feet = this.oy + (nrows - (b + n)) * z; // (her feet: the bottom of her row)
    for (let e = 0; e < n; e++) { // her eggs, under her
      const ey = this.oy + (nrows - 1 - (b + e) + 0.5) * z;
      ctx.fillStyle = C.egg; ctx.strokeStyle = C.eggRim; ctx.lineWidth = Math.max(1, z / 12);
      ctx.beginPath(); ctx.ellipse(x, ey, z * 0.36, z * 0.46, 0, 0, Math.PI * 2); ctx.fill(); ctx.stroke();
    }
    // her: two cells tall, facing her way
    const bh = z * 1.9, bw = z * 0.9, cy = feet - bh * 0.48;
    ctx.save(); ctx.shadowColor = '#fff'; ctx.shadowBlur = 14;
    ctx.fillStyle = '#fbf7ff'; ctx.beginPath(); ctx.ellipse(x, cy, bw * 0.5, bh * 0.48, 0, 0, Math.PI * 2); ctx.fill(); ctx.restore();
    ctx.fillStyle = '#ff2bd6'; ctx.beginPath(); ctx.arc(x + d * bw * 0.05, cy - bh * 0.48, z * 0.2, 0, Math.PI * 2); ctx.fill(); // comb
    ctx.fillStyle = '#ffb43a'; ctx.beginPath(); ctx.moveTo(x + d * bw * 0.42, cy - bh * 0.22); ctx.lineTo(x + d * bw * 0.78, cy - bh * 0.15); ctx.lineTo(x + d * bw * 0.42, cy - bh * 0.06); ctx.fill(); // beak
    ctx.fillStyle = '#1a1030'; ctx.beginPath(); ctx.arc(x + d * bw * 0.2, cy - bh * 0.24, Math.max(1.2, z * 0.08), 0, Math.PI * 2); ctx.fill(); // eye
  }

  // the play-test's start in the maze: the hen in green at her cell, an arrow her way
  startHen(c, b, dir, nrows, hot) {
    const { ctx, z } = this;
    const x = this.ox + (c + 0.5) * z, feet = this.oy + (nrows - b) * z, bh = z * 1.75, bw = z * 0.9, cy = feet - bh * 0.5;
    const pulse = 0.5 + 0.5 * Math.sin(performance.now() / 160);
    ctx.save();
    ctx.strokeStyle = `rgba(61,255,138,${(0.5 + 0.5 * pulse).toFixed(3)})`; ctx.lineWidth = 2; ctx.setLineDash([4, 3]);
    ctx.strokeRect(this.ox + c * z + 1, this.oy + (nrows - b - 2) * z + 1, z - 2, 2 * z - 2);
    ctx.setLineDash([]);
    ctx.shadowColor = '#3dff8a'; ctx.shadowBlur = hot ? 18 : 10;
    ctx.fillStyle = '#d9ffe8'; ctx.beginPath(); ctx.ellipse(x, cy, bw * 0.5, bh * 0.48, 0, 0, Math.PI * 2); ctx.fill();
    ctx.shadowBlur = 0;
    ctx.fillStyle = '#ff2bd6'; ctx.beginPath(); ctx.arc(x + dir * bw * 0.05, cy - bh * 0.48, z * 0.2, 0, Math.PI * 2); ctx.fill();
    ctx.fillStyle = '#ffb43a'; ctx.beginPath(); ctx.moveTo(x + dir * bw * 0.42, cy - bh * 0.22); ctx.lineTo(x + dir * bw * 0.8, cy - bh * 0.15); ctx.lineTo(x + dir * bw * 0.42, cy - bh * 0.06); ctx.fill();
    ctx.fillStyle = '#1a1030'; ctx.beginPath(); ctx.arc(x + dir * bw * 0.2, cy - bh * 0.24, Math.max(1.2, z * 0.08), 0, Math.PI * 2); ctx.fill();
    const ay = cy - bh * 0.85, ax = x + dir * (bw * 0.2 + 4 * pulse);
    ctx.fillStyle = '#3dff8a'; ctx.beginPath(); ctx.moveTo(ax + dir * 10, ay); ctx.lineTo(ax - dir * 2, ay - 6); ctx.lineTo(ax - dir * 2, ay + 6); ctx.fill();
    ctx.restore();
  }

  rulers(rows, hover) {
    const { ctx, z } = this;
    const n = cols(rows);
    ctx.fillStyle = C.ruler; ctx.fillRect(0, 0, this.w, RULER_TOP); ctx.fillRect(0, 0, RULER_LEFT, this.h);
    ctx.font = '600 10px "Chakra Petch", sans-serif'; ctx.textBaseline = 'middle';
    const step = z >= 22 ? 2 : z >= 12 ? 5 : z >= 7 ? 10 : 20;
    const v = this.visible();
    ctx.textAlign = 'center';
    for (let c = v.c0; c <= v.c1; c++) {
      const x = this.ox + (c + 0.5) * z;
      const hot = hover && hover.c === c;
      if (c % step === 0 || hot) { ctx.fillStyle = hot ? C.rulerHot : C.rulerText; ctx.fillText(String(c), x, RULER_TOP / 2 + 1); }
    }
    ctx.textAlign = 'right';
    for (let i = v.i0; i <= v.i1; i++) {
      const r = rows.length - 1 - i, y = this.oy + (i + 0.5) * z;
      const hot = hover && hover.i === i;
      if (r % (step >= 10 ? 5 : 1) === 0 || hot) { ctx.fillStyle = hot ? C.rulerHot : C.rulerText; ctx.fillText(String(r), RULER_LEFT - 5, y); }
    }
    ctx.fillStyle = C.ruler; ctx.fillRect(0, 0, RULER_LEFT, RULER_TOP);
    ctx.strokeStyle = 'rgba(255,255,255,0.08)'; ctx.lineWidth = 1;
    line(ctx, 0, RULER_TOP + 0.5, this.w, RULER_TOP + 0.5); line(ctx, RULER_LEFT + 0.5, 0, RULER_LEFT + 0.5, this.h);
    if (!n) return;
  }

  // ---- the minimap: the whole gauntlet, the bot's way on it, and what's on screen
  minimap(rows, run, fresh) {
    const ctx = this.mctx;
    const r = this.mini.getBoundingClientRect();
    const W = r.width, H = r.height, n = cols(rows);
    ctx.fillStyle = '#05030a'; ctx.fillRect(0, 0, W, H);
    if (!n) return;
    const s = Math.min((W - 16) / n, (H - 8) / rows.length);
    const ox = (W - n * s) / 2, oy = (H - rows.length * s) / 2;
    this.miniBox = { ox, oy, s };
    const tint = { '#': '#3a2711', S: '#ff4426', H: '#0d4a59', C: '#ff4426', B: '#ff2bd6', R: '#b45cff', L: '#b45cff', '>': C.entry, X: C.exit };
    ctx.fillStyle = '#120b20'; ctx.fillRect(ox, oy, n * s, rows.length * s);
    for (let i = 0; i < rows.length; i++)
      for (let c = 0; c < n; c++) {
        const t = tint[rows[i][c]];
        if (t) { ctx.fillStyle = t; ctx.fillRect(ox + c * s, oy + i * s, Math.ceil(s), Math.ceil(s)); }
      }
    if (run && fresh && run.steps.length) {
      ctx.strokeStyle = 'rgba(41,231,255,0.9)'; ctx.lineWidth = 1.2; ctx.beginPath();
      run.steps.forEach(([c, b, nn], k) => { const x = ox + (c + 0.5) * s, y = oy + (rows.length - 1 - (b + nn) + 0.5) * s; k ? ctx.lineTo(x, y) : ctx.moveTo(x, y); });
      ctx.stroke();
    }
    // what's on screen
    const vx = ox + ((RULER_LEFT - this.ox) / this.z) * s, vy = oy + ((RULER_TOP - this.oy) / this.z) * s;
    const vw = ((this.w - RULER_LEFT) / this.z) * s, vh = ((this.h - RULER_TOP) / this.z) * s;
    ctx.strokeStyle = '#f4ff5a'; ctx.lineWidth = 1.5; ctx.strokeRect(vx, vy, vw, vh);
    ctx.fillStyle = 'rgba(244,255,90,0.06)'; ctx.fillRect(vx, vy, vw, vh);
  }

  miniToCell(px, py) {
    const b = this.miniBox;
    if (!b) return null;
    return { c: (px - b.ox) / b.s, i: (py - b.oy) / b.s };
  }
}

function line(ctx, x0, y0, x1, y1) { ctx.beginPath(); ctx.moveTo(x0, y0); ctx.lineTo(x1, y1); ctx.stroke(); }
