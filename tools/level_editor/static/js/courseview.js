// The level editor's course canvas: a level side on, as the game lays it (Level::load). Its walls of crates and hay
// (the zone's look), their tops the disco dance floor, barriers' hazard-striped girders under their red laser curtains,
// saw blades spinning in the wall tops, disco balls, the maze gauntlet's grid in its span, the finish line. Over it
// what the checker and the game's bot found: the bot's stack all along its run, its near misses, where it crashed;
// notes; the hen herself in the replay. Plus the minimap: the whole course.
import { GridView, C as GC } from './view.js';
import { U, colAt, spans, courseLength, tallest } from './course.js';

export const RULER_TOP = 22, RULER_LEFT = 34;

export const ZONE_LOOK = {
  city: { sky: ['#06030e', '#120a26'], accent: '#29e7ff', crate: '#0e1a2c', crateLit: '#13243c', crateEdge: 'rgba(41,231,255,0.55)', ground: '#05030b' },
  egypt: { sky: ['#07040c', '#1d1124'], accent: '#ffc23a', crate: '#22170b', crateLit: '#2f2010', crateEdge: 'rgba(226,164,60,0.7)', ground: '#0a0604' },
  japan: { sky: ['#08030c', '#230a22'], accent: '#ff2bd6', crate: '#200a1d', crateLit: '#2c0f28', crateEdge: 'rgba(255,43,214,0.55)', ground: '#08030a' },
};
const HAY = { fill: '#4a3510', lit: '#5a4114', straw: 'rgba(255,194,58,0.45)', edge: 'rgba(255,211,106,0.7)' };
const DISCO = ['#ff2bd6', '#29e7ff', '#f4ff5a'];
export const K_COLORS = { perfect: '#ff2bd6', call: '#29e7ff', crash: '#ff2b4a', note: '#ffb43a', short: '#ff2b4a', bot: '#29e7ff', egg: '#fff3dc' };

const cellOf = GridView.prototype.cell, bladeOf = GridView.prototype.blade;

export class CourseView {
  constructor(canvas, minimap) {
    this.canvas = canvas;
    this.ctx = canvas.getContext('2d');
    this.mini = minimap;
    this.mctx = minimap.getContext('2d');
    this.z = 16;                       // a cell, CSS px
    this.ox = RULER_LEFT + 20;         // x = 0, CSS px
    this.oy = 400;                     // the street, CSS px
    this.w = 1; this.h = 1;
    this.cols = 1; this.rowsUp = 20;   // (the course's size, for clamping)
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

  // ---- the view
  X(cx) { return this.ox + cx * this.z; }
  Y(cy) { return this.oy - cy * this.z; }
  at(px, py) { // a point: its column and row (row 0 the first over the street), and where in metres
    const cx = (px - this.ox) / this.z, cy = (this.oy - py) / this.z;
    return { c: Math.floor(cx), r: Math.floor(cy), x: cx * U, y: cy * U };
  }
  measure(L, grids) {
    this.cols = Math.max(L.cols.length, Math.ceil(L.length / U));
    let up = tallest(L) + 4;
    for (const s of spans(L)) if (grids[s.name]) up = Math.max(up, grids[s.name].length + 3);
    this.rowsUp = Math.max(14, up);
  }
  fit(L, grids, x = null) { // as tall as the course's tallest thing; at x (metres) if given, else its start
    this.measure(L, grids);
    const room = this.h - RULER_TOP - 40;
    this.z = Math.max(5, Math.min(26, Math.floor(room / this.rowsUp)));
    this.oy = Math.round(this.h - 30);
    if (x === null) this.ox = RULER_LEFT + 24;
    else this.ox = (RULER_LEFT + this.w) / 2 - (x / U) * this.z;
    this.clamp();
  }
  zoomAt(px, py, factor) {
    const z = Math.max(2, Math.min(64, this.z * factor));
    this.ox = px - ((px - this.ox) * z) / this.z;
    this.oy = py + ((this.oy - py) * z) / this.z;
    this.z = z;
    this.clamp();
  }
  panBy(dx, dy) { this.ox += dx; this.oy += dy; this.clamp(); }
  centerOnX(x) { this.ox = (RULER_LEFT + this.w) / 2 - (x / U) * this.z; this.clamp(); }
  centerOn(x, y) { this.centerOnX(x); this.oy = (RULER_TOP + this.h) / 2 + (y / U) * this.z; this.clamp(); }
  clamp() {
    const total = this.cols * this.z;
    this.ox = Math.min(RULER_LEFT + 240, Math.max(this.w - 240 - total, this.ox));
    this.oy = Math.min(this.h + this.rowsUp * this.z - 80, Math.max(RULER_TOP + 80, this.oy));
  }
  visibleCols() { return { c0: Math.max(0, Math.floor((RULER_LEFT - this.ox) / this.z) - 1), c1: Math.ceil((this.w - this.ox) / this.z) + 1 }; }
  onScreen(x) { const px = this.X(x / U); return px > RULER_LEFT + 20 && px < this.w - 20; }

  // ---- drawing
  draw(s) {
    const { ctx, z } = this, L = s.L, look = ZONE_LOOK[s.zone] || ZONE_LOOK.city, t = s.time || 0;
    this._L = L;
    this.measure(L, s.grids);
    const { c0, c1 } = this.visibleCols();
    const X = (cx) => this.X(cx), Y = (cy) => this.Y(cy);
    // the night sky over the course, the ground under it
    const sky = ctx.createLinearGradient(0, RULER_TOP, 0, this.oy);
    sky.addColorStop(0, look.sky[0]); sky.addColorStop(1, look.sky[1]);
    ctx.fillStyle = sky; ctx.fillRect(0, 0, this.w, this.h);
    const glow = ctx.createLinearGradient(0, this.oy - z * 6, 0, this.oy);
    glow.addColorStop(0, 'rgba(0,0,0,0)'); glow.addColorStop(1, hexA(look.accent, 0.08));
    ctx.fillStyle = glow; ctx.fillRect(0, this.oy - z * 6, this.w, z * 6);
    ctx.fillStyle = look.ground; ctx.fillRect(0, this.oy, this.w, this.h - this.oy);
    // the grid's faint lines
    if (s.overlays.grid && z >= 6) {
      ctx.lineWidth = 1;
      for (let c = c0; c <= c1; c++) { ctx.strokeStyle = c % 10 === 0 ? 'rgba(255,255,255,0.06)' : 'rgba(255,255,255,0.025)'; line(ctx, X(c) + 0.5, RULER_TOP, X(c) + 0.5, this.oy); }
      for (let r = 1; r <= this.rowsUp + 20; r++) { const y = Y(r); if (y < RULER_TOP) break; ctx.strokeStyle = r % 10 === 0 ? 'rgba(255,255,255,0.06)' : 'rgba(255,255,255,0.025)'; line(ctx, Math.max(RULER_LEFT, X(c0)), y + 0.5, X(c1), y + 0.5); }
    }
    ctx.strokeStyle = hexA(look.accent, 0.55); ctx.lineWidth = 1.5; line(ctx, RULER_LEFT, this.oy + 0.75, this.w, this.oy + 0.75); // (the street)
    // the walls, their dance-floor tops, the barriers over them
    const cols = L.cols;
    for (let c = Math.max(0, c0); c <= Math.min(cols.length - 1, c1); c++) {
      const k = cols[c];
      if (k.g || !k.h) continue;
      const x = X(c);
      if (z < 6) {
        ctx.fillStyle = k.kind === 'hay' ? HAY.fill : look.crateLit; ctx.fillRect(x, Y(k.h), z + 0.5, k.h * z);
        if (k.kind === 'mix') { ctx.fillStyle = HAY.fill; ctx.fillRect(x, Y(k.h), z + 0.5, z); }
        continue;
      }
      for (let j = 0; j < k.h; j++) this.block(k.kind === 'hay' || (k.kind === 'mix' && j === k.h - 1) ? 'hay' : 'crate', x, Y(j + 1), look, c, j);
    }
    // wall silhouettes: edged where a wall meets lower ground (rooms read as carved)
    ctx.lineWidth = Math.max(1, Math.min(2, z / 10));
    for (let c = Math.max(0, c0); c <= Math.min(cols.length - 1, c1); c++) {
      const k = cols[c];
      if (k.g) continue;
      const h = k.h, left = c > 0 && !cols[c - 1].g ? cols[c - 1].h : 0, right = c + 1 < cols.length && !cols[c + 1].g ? cols[c + 1].h : 0;
      ctx.strokeStyle = k.kind === 'hay' ? HAY.edge : look.crateEdge;
      if (h > left) line(ctx, X(c) + 0.5, Y(left), X(c) + 0.5, Y(h));
      if (h > right) line(ctx, X(c + 1) - 0.5, Y(right), X(c + 1) - 0.5, Y(h));
      if (h > 0) { // the dance floor: its tiles cycling through the disco's colours
        const tile = DISCO[(c + Math.floor(t * 5)) % 3];
        ctx.fillStyle = hexA(tile, 0.85); ctx.fillRect(X(c) + 1, Y(h) - Math.max(1.5, z * 0.09), z - 2, Math.max(1.5, z * 0.09));
      }
    }
    for (let c = Math.max(0, c0); c <= Math.min(cols.length - 1, c1); c++) if (cols[c].ceil && !cols[c].g) this.barrier(c, cols[c].ceil, t);
    // the maze gauntlets: each its grid, in its span
    for (const sp of spans(L)) this.gauntlet(sp, s.grids[sp.name], s.hoverSpan === sp.name, t, c0, c1);
    // changed since the last save: the saved course as a dashed ghost where it differs (describe.js diffCourse)
    if (s.overlays.changes && s.diff) this.changes(s.diff, c0, c1, t);
    // saws in the wall tops, spinning; disco balls
    L.saws.forEach((x, i) => {
      const cx = x / U;
      if (cx < c0 - 2 || cx > c1 + 2) return;
      const k = cols[colAt(x)], h = k && !k.g ? k.h : 0;
      this.saw(X(cx), Y(h), h > 0 && (k.kind === 'hay' || k.kind === 'mix'), t, s.selected && s.selected.saw === i);
    });
    L.discos.forEach((d, i) => {
      const cx = d.x / U;
      if (cx < c0 - 2 || cx > c1 + 2) return;
      this.disco(X(cx), Y(d.y / U + Math.sin(t * 3 + d.x * 1.7) * 0.1), t, i, s.selected && s.selected.disco === i);
    });
    // the finish line; past it, the runway
    this.finish(L.length, t, s.hoverFinish);
    const fx = X(L.length / U);
    if (fx < this.w) { ctx.fillStyle = 'rgba(4,2,9,0.55)'; ctx.fillRect(Math.max(fx, RULER_LEFT), RULER_TOP, this.w - Math.max(fx, RULER_LEFT), this.h - RULER_TOP); }
    // the play-test's start: a flag on the course, or the hen at her cell in the maze
    if (s.start) {
      if (s.start.x !== undefined) this.startFlag(s.start.x, t, s.hoverStart);
      else { const sp = spans(L).find((q) => q.name === s.start.gauntlet); if (sp) this.startHen(sp.c0 + s.start.c, s.start.b, s.start.dir, t, s.hoverStart); }
    }
    // what the game's bot did: her stack all along its run (its floor, its top), its near misses, where it crashed
    if (s.overlays.bot && s.game && s.game.trace) this.botRun(s.game, s.stale);
    // notes: where the course reads unfairly, where the egg budget runs short, a gauntlet's door off its height
    if (s.overlays.notes && s.check && s.check.ok) {
      for (const n of s.check.notes) this.marker(n.x, n.y, K_COLORS.note, '!');
      for (const n of s.check.short) this.marker(n.x, n.y, K_COLORS.short, '!', true);
      for (const n of s.check.doors || []) this.marker(n.x, n.y, K_COLORS.short, '!', true);
    }
    if (s.selection) this.band(s.selection.c0, s.selection.c1, '#f4ff5a', true, t);
    if (s.preview) this.preview(s.preview, look, t);
    if (s.hover && !s.preview && s.hover.c >= 0 && s.hover.c < cols.length) {
      ctx.fillStyle = 'rgba(255,255,255,0.035)'; ctx.fillRect(X(s.hover.c), RULER_TOP, z, this.oy - RULER_TOP);
      if (s.hover.r >= 0) { ctx.strokeStyle = 'rgba(255,255,255,0.8)'; ctx.lineWidth = 1.5; ctx.strokeRect(X(s.hover.c) + 0.75, Y(s.hover.r + 1) + 0.75, z - 1.5, z - 1.5); }
    }
    if (s.focus) { // (a History entry hovered: where its edit was, pulsing)
      const a = Math.max(RULER_LEFT, X(s.focus[0] / U) - 3), b = Math.min(this.w, X(s.focus[1] / U) + 3), pulse = 0.5 + 0.5 * Math.sin(t * 7);
      if (b > a) {
        ctx.fillStyle = `rgba(244,255,90,${(0.08 + 0.08 * pulse).toFixed(3)})`; ctx.fillRect(a, RULER_TOP, b - a, this.oy - RULER_TOP);
        ctx.strokeStyle = `rgba(244,255,90,${(0.55 + 0.4 * pulse).toFixed(3)})`; ctx.lineWidth = 2; ctx.strokeRect(a + 1, RULER_TOP + 1, b - a - 2, this.oy - RULER_TOP - 2);
      }
    }
    if (s.replay) this.hen(s.replay, t);
    this.rulers(s.hover, L);
  }

  block(kind, x, y, look, c, j) {
    const { ctx, z } = this;
    if (kind === 'hay') {
      ctx.fillStyle = HAY.fill; ctx.fillRect(x, y, z, z);
      if (z >= 9) {
        ctx.fillStyle = HAY.lit; ctx.fillRect(x + z * 0.08, y + z * 0.08, z * 0.84, z * 0.84);
        ctx.strokeStyle = HAY.straw; ctx.lineWidth = 1;
        for (const f of [0.3, 0.55, 0.8]) line(ctx, x + z * 0.12, y + z * f + ((c * 7 + j * 3) % 3) - 1, x + z * 0.88, y + z * f + ((c * 5 + j) % 3) - 1);
      }
    } else {
      ctx.fillStyle = look.crate; ctx.fillRect(x, y, z, z);
      if (z >= 9) {
        ctx.fillStyle = look.crateLit; ctx.fillRect(x + z * 0.1, y + z * 0.1, z * 0.8, z * 0.8);
        ctx.strokeStyle = 'rgba(0,0,0,0.35)'; ctx.lineWidth = 1;
        if ((c + j) % 2) line(ctx, x + z * 0.18, y + z * 0.18, x + z * 0.82, y + z * 0.82); else line(ctx, x + z * 0.82, y + z * 0.18, x + z * 0.18, y + z * 0.82);
      }
    }
    ctx.strokeStyle = 'rgba(0,0,0,0.5)'; ctx.lineWidth = 1; ctx.strokeRect(x + 0.5, y + 0.5, z - 1, z - 1);
  }

  barrier(c, ceil, t) {
    const { ctx, z } = this;
    const x = this.X(c), y = this.Y(ceil + 1), h = z;
    // the red laser curtain over it, flickering
    const cur = ctx.createLinearGradient(0, y - z * 2.5, 0, y);
    const a = 0.16 + 0.06 * Math.sin(t * 14 + c * 0.9);
    cur.addColorStop(0, 'rgba(255,43,74,0)'); cur.addColorStop(1, `rgba(255,43,74,${a.toFixed(3)})`);
    ctx.fillStyle = cur; ctx.fillRect(x, y - z * 2.5, z, z * 2.5);
    // the girder: hazard stripes
    ctx.save();
    ctx.beginPath(); ctx.rect(x, y, z, h); ctx.clip();
    ctx.fillStyle = '#16120a'; ctx.fillRect(x, y, z, h);
    ctx.fillStyle = 'rgba(244,255,90,0.85)';
    const w = Math.max(3, z * 0.32);
    for (let k = -2; k < 4; k++) { const sx = x + k * w * 2 + ((c * z) % (w * 2)); ctx.beginPath(); ctx.moveTo(sx, y + h); ctx.lineTo(sx + w, y + h); ctx.lineTo(sx + w + h, y); ctx.lineTo(sx + h, y); ctx.fill(); }
    ctx.restore();
    // its underside: the bright edge her head must clear
    ctx.save(); ctx.shadowColor = '#29e7ff'; ctx.shadowBlur = 8;
    ctx.fillStyle = '#9ef6ff'; ctx.fillRect(x, this.Y(ceil) - 2, z, 2.5);
    ctx.restore();
  }

  gauntlet(sp, rows, hot, t, c0, c1) {
    const { ctx, z } = this;
    const x0 = this.X(sp.c0), width = sp.c1 - sp.c0;
    if (!rows) { ctx.fillStyle = 'rgba(255,194,58,0.12)'; ctx.fillRect(x0, this.Y(12), width * z, 12 * z); return; }
    const n = rows.length, fake = { ctx, z };
    const tint = { '#': '#3a2711', S: '#ff4426', H: '#0d4a59', C: '#ff4426', B: '#ff2bd6', R: '#b45cff', L: '#b45cff', '>': '#3dff8a', X: '#f4ff5a' };
    const a = Math.max(sp.c0, c0) - sp.c0, b = Math.min(sp.c1 - 1, c1) - sp.c0;
    ctx.fillStyle = '#0e0919'; ctx.fillRect(this.X(sp.c0 + Math.max(0, a)), this.Y(n), (Math.max(0, b - a + 1)) * z, n * z);
    for (let i = 0; i < n; i++) {
      const y = this.Y(n - i);
      if (y > this.h || y + z < RULER_TOP) continue;
      for (let c = Math.max(0, a); c <= Math.min(width - 1, b); c++) {
        const ch = rows[i][c];
        if (ch === '.' || ch === undefined) continue;
        if (z < 6) { ctx.fillStyle = tint[ch] || '#3a2711'; ctx.fillRect(this.X(sp.c0 + c), y, z + 0.5, z + 0.5); }
        else cellOf.call(fake, ch, this.X(sp.c0 + c), y, z);
      }
    }
    if (z >= 6) for (let i = 0; i < n; i++) for (let c = Math.max(0, a); c <= Math.min(width - 1, b); c++) if (rows[i][c] === 'S' || rows[i][c] === 'C') bladeOf.call(fake, this.X(sp.c0 + c) + z / 2, this.Y(n - i), z);
    // its frame, and its name plate
    ctx.save();
    ctx.strokeStyle = hot ? '#ffd36a' : 'rgba(255,194,58,0.6)'; ctx.lineWidth = hot ? 2 : 1.25; ctx.setLineDash(hot ? [] : [6, 4]);
    ctx.strokeRect(x0 + 0.5, this.Y(n) + 0.5, width * z - 1, n * z - 1);
    ctx.restore();
    const label = `${sp.name.toUpperCase()}  ${width} × ${n}`, hint = 'double-click: edit its grid';
    ctx.font = '900 12px "Cuckoo Display", "Chakra Petch", sans-serif';
    const lw = ctx.measureText(label).width;
    ctx.font = '700 11px "Chakra Petch", sans-serif';
    const hw = hot ? ctx.measureText(hint).width + 16 : 0, tw = lw + 22 + hw;
    const lx = Math.max(RULER_LEFT + 6, Math.min(x0 + 6, x0 + width * z - tw - 6)), ly = this.Y(n) - 26;
    if (ly > RULER_TOP - 4) {
      ctx.fillStyle = hot ? 'rgba(255,194,58,0.95)' : 'rgba(30,20,6,0.92)'; roundRect(ctx, lx, ly, tw, 20, 6); ctx.fill();
      ctx.strokeStyle = 'rgba(255,194,58,0.8)'; ctx.lineWidth = 1; roundRect(ctx, lx + 0.5, ly + 0.5, tw - 1, 19, 6); ctx.stroke();
      ctx.fillStyle = hot ? '#1a1000' : '#ffc23a'; ctx.textBaseline = 'middle'; ctx.textAlign = 'left';
      ctx.font = '900 12px "Cuckoo Display", "Chakra Petch", sans-serif';
      ctx.fillText(label, lx + 11, ly + 10.5);
      if (hot) {
        ctx.fillStyle = 'rgba(26,16,0,0.35)'; ctx.fillRect(lx + 11 + lw + 7, ly + 5, 1, 11);
        ctx.fillStyle = '#1a1000'; ctx.font = '700 11px "Chakra Petch", sans-serif'; ctx.fillText(hint, lx + 11 + lw + 15, ly + 10.5);
      }
    }
  }

  saw(cx, cy, cube, t, selected) {
    const { ctx, z } = this;
    const r = (0.5 / U) * z, spin = t * 9;
    ctx.save();
    ctx.beginPath(); ctx.rect(cx - r * 1.4, cy - r * 1.5, r * 2.8, r * 1.5); ctx.clip(); // (its hub at the surface: half of it shows)
    ctx.shadowColor = GC.blade; ctx.shadowBlur = Math.min(14, z * 0.8);
    ctx.fillStyle = GC.blade; ctx.beginPath(); ctx.arc(cx, cy, r * 0.92, 0, Math.PI * 2); ctx.fill();
    ctx.shadowBlur = 0;
    if (z >= 7) {
      for (let k = 0; k < 14; k++) { // teeth, turning
        const a = spin + (k * Math.PI * 2) / 14, tip = r * 1.16;
        ctx.beginPath(); ctx.moveTo(cx + Math.cos(a - 0.13) * r * 0.9, cy + Math.sin(a - 0.13) * r * 0.9);
        ctx.lineTo(cx + Math.cos(a + 0.05) * tip, cy + Math.sin(a + 0.05) * tip); ctx.lineTo(cx + Math.cos(a + 0.16) * r * 0.9, cy + Math.sin(a + 0.16) * r * 0.9); ctx.fill();
      }
      ctx.strokeStyle = 'rgba(255,207,154,0.55)'; ctx.lineWidth = 1;
      for (let k = 0; k < 4; k++) { const a = spin * 0.7 + (k * Math.PI) / 2; line(ctx, cx + Math.cos(a) * r * 0.32, cy + Math.sin(a) * r * 0.32, cx + Math.cos(a) * r * 0.78, cy + Math.sin(a) * r * 0.78); }
    }
    ctx.fillStyle = GC.bladeHot; ctx.beginPath(); ctx.arc(cx, cy, r * 0.3, 0, Math.PI * 2); ctx.fill();
    if (cube) { ctx.strokeStyle = '#29e7ff'; ctx.lineWidth = Math.max(1.5, z / 9); ctx.beginPath(); ctx.arc(cx, cy, r * 0.42, Math.PI, 0); ctx.stroke(); }
    ctx.restore();
    if (selected) { ctx.save(); ctx.strokeStyle = '#f4ff5a'; ctx.lineWidth = 2; ctx.setLineDash([4, 3]); ctx.lineDashOffset = -t * 20; ctx.beginPath(); ctx.arc(cx, cy, r * 1.35, Math.PI, 0); ctx.stroke(); ctx.restore(); }
  }

  disco(cx, cy, t, i, selected) {
    const { ctx, z } = this;
    const r = Math.max(3, (0.3 / U) * z);
    const halo = ctx.createRadialGradient(cx, cy, r * 0.5, cx, cy, r * 2.6);
    halo.addColorStop(0, hexA(DISCO[i % 3], 0.35)); halo.addColorStop(1, 'rgba(0,0,0,0)');
    ctx.fillStyle = halo; ctx.beginPath(); ctx.arc(cx, cy, r * 2.6, 0, Math.PI * 2); ctx.fill();
    const g = ctx.createRadialGradient(cx - r * 0.35, cy - r * 0.4, r * 0.1, cx, cy, r);
    g.addColorStop(0, '#ffffff'); g.addColorStop(0.45, '#c9c4ea'); g.addColorStop(1, '#4a4672');
    ctx.fillStyle = g; ctx.beginPath(); ctx.arc(cx, cy, r, 0, Math.PI * 2); ctx.fill();
    if (r >= 5) { // its mirror facets, turning
      ctx.save(); ctx.beginPath(); ctx.arc(cx, cy, r, 0, Math.PI * 2); ctx.clip();
      ctx.strokeStyle = 'rgba(40,30,80,0.45)'; ctx.lineWidth = 0.8;
      for (let k = -2; k <= 2; k++) line(ctx, cx - r, cy + (k * r) / 2.5, cx + r, cy + (k * r) / 2.5);
      const off = ((t * 0.6) % 1) * (r / 1.5);
      for (let k = -3; k <= 3; k++) { const xx = cx + k * (r / 1.5) + off; ctx.beginPath(); ctx.ellipse(cx, cy, Math.abs(xx - cx) + 0.1, r, 0, 0, Math.PI * 2); ctx.stroke(); }
      ctx.restore();
    }
    const tw = (Math.sin(t * 6 + i * 2.3) + 1) / 2; // a glint, twinkling
    if (tw > 0.55) star(ctx, cx + r * 0.45, cy - r * 0.5, r * 0.9 * tw, '#fff');
    if (selected) { ctx.strokeStyle = '#f4ff5a'; ctx.lineWidth = 2; ctx.setLineDash([4, 3]); ctx.beginPath(); ctx.arc(cx, cy, r * 1.8, 0, Math.PI * 2); ctx.stroke(); ctx.setLineDash([]); }
  }

  finish(length, t, hot) {
    const { ctx, z } = this;
    const x = this.X(length / U);
    if (x < RULER_LEFT - 40 || x > this.w + 40) return;
    const top = this.Y(Math.max(10, Math.min(this.rowsUp - 1, 14)));
    const sq = Math.max(3, z * 0.3);
    for (let y = this.oy - sq, k = 0; y > top; y -= sq, k++) {
      ctx.fillStyle = k % 2 ? '#fff' : '#111'; ctx.fillRect(x - sq, y, sq, sq);
      ctx.fillStyle = k % 2 ? '#111' : '#fff'; ctx.fillRect(x, y, sq, sq);
    }
    // the banner, waving
    const wave = Math.sin(t * 4) * 3;
    ctx.save(); ctx.shadowColor = '#f4ff5a'; ctx.shadowBlur = hot ? 18 : 8;
    ctx.fillStyle = hot ? '#fbff9a' : '#f4ff5a';
    ctx.beginPath(); ctx.moveTo(x + sq, top); ctx.lineTo(x + sq + 86, top + wave); ctx.lineTo(x + sq + 86, top + 22 + wave); ctx.lineTo(x + sq, top + 22); ctx.fill();
    ctx.restore();
    ctx.fillStyle = '#1a1a00'; ctx.font = '900 11px "Cuckoo Display", "Chakra Petch", sans-serif'; ctx.textBaseline = 'middle'; ctx.textAlign = 'left';
    ctx.fillText(`FINISH ${Math.round(length)} m`, x + sq + 7, top + 11 + wave / 2);
    ctx.fillStyle = hot ? '#fff' : '#f4ff5a'; ctx.beginPath(); ctx.arc(x, top - 6, hot ? 7 : 5, 0, Math.PI * 2); ctx.fill(); // (the handle)
    ctx.strokeStyle = '#1a1a00'; ctx.lineWidth = 1.5; ctx.stroke();
  }
  finishHandle(length) { const top = this.Y(Math.max(10, Math.min(this.rowsUp - 1, 14))); return { x: this.X(length / U), y: top - 6, top }; }

  // what's changed since the save: the saved walls dashed where they differ, new ones tinted, columns taken out marked,
  // saws and disco balls added ringed green and removed ones ghosted in red, the saved finish line a ghost flag
  changes(d, c0, c1, t) {
    const { ctx, z } = this;
    const X = (cx) => this.X(cx), Y = (cy) => this.Y(cy);
    ctx.save();
    for (const r of d.inserted) {
      if (r.c1 < c0 || r.c0 > c1) continue;
      ctx.fillStyle = 'rgba(61,255,138,0.08)'; ctx.fillRect(X(r.c0), RULER_TOP, (r.c1 - r.c0 + 1) * z, this.oy - RULER_TOP);
      ctx.strokeStyle = 'rgba(61,255,138,0.75)'; ctx.lineWidth = 1.5; ctx.setLineDash([5, 4]); ctx.strokeRect(X(r.c0) + 0.75, RULER_TOP + 1, (r.c1 - r.c0 + 1) * z - 1.5, this.oy - RULER_TOP - 2);
    }
    ctx.setLineDash([4, 3]); ctx.lineDashOffset = -(t * 12) % 7;
    for (const r of d.replaced) {
      if (r.c1 < c0 || r.c0 > c1) continue;
      r.was.forEach((k, i) => { // (the saved column's wall and barrier, as they were)
        const x = X(r.c0 + i);
        ctx.fillStyle = 'rgba(244,255,90,0.07)'; ctx.fillRect(x, Y(Math.max(k.h, 0.15)), z, Math.max(k.h, 0.15) * z);
        ctx.strokeStyle = 'rgba(255,255,255,0.85)'; ctx.lineWidth = 1.5;
        if (k.h) ctx.strokeRect(x + 1, Y(k.h) + 1, z - 2, k.h * z - 2); else { ctx.beginPath(); ctx.moveTo(x + 1, this.oy - 1); ctx.lineTo(x + z - 1, this.oy - 1); ctx.stroke(); }
        if (k.ceil) { ctx.strokeStyle = 'rgba(158,246,255,0.9)'; ctx.strokeRect(x + 1, Y(k.ceil + 1) + 1, z - 2, z - 2); }
      });
    }
    ctx.setLineDash([]);
    for (const r of d.removed) { // (columns taken out: where they were)
      const x = X(r.at);
      if (x < RULER_LEFT || x > this.w) continue;
      ctx.strokeStyle = '#ff2b4a'; ctx.lineWidth = 2.5; ctx.beginPath(); ctx.moveTo(x, RULER_TOP + 4); ctx.lineTo(x, this.oy); ctx.stroke();
      const label = `−${r.n}`;
      ctx.font = '700 10px "Chakra Petch", sans-serif'; const w = ctx.measureText(label).width + 10;
      ctx.fillStyle = '#ff2b4a'; roundRect(ctx, x - w / 2, RULER_TOP + 6, w, 16, 5); ctx.fill();
      ctx.fillStyle = '#fff'; ctx.textAlign = 'center'; ctx.textBaseline = 'middle'; ctx.fillText(label, x, RULER_TOP + 14.5);
    }
    const ring = (x, y, color, dashed) => {
      ctx.strokeStyle = color; ctx.lineWidth = 2; ctx.setLineDash(dashed ? [3, 3] : []);
      ctx.beginPath(); ctx.arc(x, y, Math.max(7, z * 0.62), 0, Math.PI * 2); ctx.stroke(); ctx.setLineDash([]);
    };
    const cols = this._L ? this._L.cols : null;
    const top = (x) => { const k = cols && cols[colAt(x)]; return k && !k.g ? k.h : 0; };
    for (const x of d.saws.added) ring(X(x / U), Y(top(x)) - z * 0.25, '#3dff8a', false);
    for (const x of d.saws.removed) {
      const px = X(x / U), py = Y(top(x)) - z * 0.25;
      ctx.globalAlpha = 0.45; this.saw(px, Y(top(x)), false, 0, false); ctx.globalAlpha = 1;
      ring(px, py, '#ff2b4a', true);
      ctx.strokeStyle = '#ff2b4a'; ctx.lineWidth = 2; const r = Math.max(4, z * 0.3);
      ctx.beginPath(); ctx.moveTo(px - r, py - r); ctx.lineTo(px + r, py + r); ctx.moveTo(px + r, py - r); ctx.lineTo(px - r, py + r); ctx.stroke();
    }
    for (const p of d.discos.added) ring(X(p.x / U), Y(p.y / U), '#3dff8a', false);
    for (const p of d.discos.removed) { ctx.globalAlpha = 0.4; this.disco(X(p.x / U), Y(p.y / U), t, 0, false); ctx.globalAlpha = 1; ring(X(p.x / U), Y(p.y / U), '#ff2b4a', true); }
    if (d.finish) { // (the saved finish line: a ghost flag)
      const x = X(d.finish.ghost / U);
      if (x > RULER_LEFT && x < this.w) {
        ctx.strokeStyle = 'rgba(244,255,90,0.6)'; ctx.lineWidth = 2; ctx.setLineDash([5, 4]);
        ctx.beginPath(); ctx.moveTo(x, this.oy); ctx.lineTo(x, Y(12)); ctx.stroke(); ctx.setLineDash([]);
        ctx.fillStyle = 'rgba(244,255,90,0.6)'; ctx.font = '700 10px "Chakra Petch", sans-serif'; ctx.textAlign = 'left'; ctx.textBaseline = 'bottom';
        ctx.fillText(`saved finish ${Math.round(d.finish.from)} m`, x + 4, Y(12));
      }
    }
    ctx.restore();
  }

  // the play-test's start on the course: a green flag, START and how far along
  startFlag(x, t, hot) {
    const { ctx, z } = this;
    const px = this.X(x / U);
    if (px < RULER_LEFT - 60 || px > this.w + 60) return;
    const k = this._L && this._L.cols[colAt(x)], floor = this.Y(k && !k.g ? k.h : 0), top = floor - Math.max(70, z * 5);
    ctx.save();
    ctx.strokeStyle = '#3dff8a'; ctx.lineWidth = 2.5; ctx.shadowColor = '#3dff8a'; ctx.shadowBlur = hot ? 16 : 8;
    ctx.beginPath(); ctx.moveTo(px, floor); ctx.lineTo(px, top); ctx.stroke();
    const wave = Math.sin(t * 4.5) * 2.5, label = `START ${Math.round(x * 10) / 10} m`;
    ctx.font = '900 11px "Cuckoo Display", "Chakra Petch", sans-serif';
    const w = ctx.measureText(label).width + 30;
    ctx.fillStyle = hot ? '#7dffad' : '#3dff8a';
    ctx.beginPath(); ctx.moveTo(px, top); ctx.lineTo(px + w, top + wave); ctx.lineTo(px + w - 7, top + 11 + wave / 2); ctx.lineTo(px + w, top + 22 + wave); ctx.lineTo(px, top + 22); ctx.fill();
    ctx.shadowBlur = 0;
    ctx.fillStyle = '#04210f'; ctx.textAlign = 'left'; ctx.textBaseline = 'middle'; ctx.fillText(label, px + 6, top + 11 + wave / 2);
    ctx.fillStyle = hot ? '#fff' : '#3dff8a'; ctx.beginPath(); ctx.arc(px, top - 6, hot ? 7 : 5, 0, Math.PI * 2); ctx.fill(); // (the handle)
    ctx.strokeStyle = '#04210f'; ctx.lineWidth = 1.5; ctx.stroke();
    ctx.restore();
  }
  startHandle(x) { const k = this._L && this._L.cols[colAt(x)], floor = this.Y(k && !k.g ? k.h : 0); return { x: this.X(x / U), y: floor - Math.max(70, this.z * 5) - 6, floor }; }

  // ...or in a maze: the hen in green at her cell, an arrow her way
  startHen(cx, b, dir, t, hot) {
    const { ctx, z } = this;
    const x = this.X(cx + 0.5), feet = this.Y(b), bh = z * 1.75, bw = z * 0.9, cy = feet - bh * 0.5;
    const pulse = 0.5 + 0.5 * Math.sin(t * 5);
    ctx.save();
    ctx.strokeStyle = `rgba(61,255,138,${(0.5 + 0.5 * pulse).toFixed(3)})`; ctx.lineWidth = 2; ctx.setLineDash([4, 3]);
    ctx.strokeRect(this.X(cx) + 1, this.Y(b + 2) + 1, z - 2, 2 * z - 2);
    ctx.setLineDash([]);
    ctx.shadowColor = '#3dff8a'; ctx.shadowBlur = hot ? 18 : 10;
    ctx.fillStyle = '#d9ffe8'; ctx.beginPath(); ctx.ellipse(x, cy, bw * 0.5, bh * 0.48, 0, 0, Math.PI * 2); ctx.fill();
    ctx.shadowBlur = 0;
    ctx.fillStyle = '#ff2bd6'; ctx.beginPath(); ctx.arc(x + dir * bw * 0.05, cy - bh * 0.48, z * 0.2, 0, Math.PI * 2); ctx.fill();
    ctx.fillStyle = '#ffb43a'; ctx.beginPath(); ctx.moveTo(x + dir * bw * 0.42, cy - bh * 0.22); ctx.lineTo(x + dir * bw * 0.8, cy - bh * 0.15); ctx.lineTo(x + dir * bw * 0.42, cy - bh * 0.06); ctx.fill();
    ctx.fillStyle = '#1a1030'; ctx.beginPath(); ctx.arc(x + dir * bw * 0.2, cy - bh * 0.24, Math.max(1.2, z * 0.08), 0, Math.PI * 2); ctx.fill();
    // her way: a green arrow
    const ay = cy - bh * 0.85, ax = x + dir * (bw * 0.2 + 4 * pulse);
    ctx.fillStyle = '#3dff8a'; ctx.beginPath(); ctx.moveTo(ax + dir * 10, ay); ctx.lineTo(ax - dir * 2, ay - 6); ctx.lineTo(ax - dir * 2, ay + 6); ctx.fill();
    ctx.restore();
  }

  botRun(game, stale) {
    const { ctx } = this;
    const tr = game.trace;
    if (!tr.length) return;
    ctx.save();
    ctx.globalAlpha = stale ? 0.35 : 1;
    const px = (x) => this.X(x / U), py = (y) => this.Y(y / U);
    // her stack, floor to top
    ctx.beginPath();
    tr.forEach((p, i) => (i ? ctx.lineTo(px(p[0]), py(p[1] + p[2] * U)) : ctx.moveTo(px(p[0]), py(p[1] + p[2] * U))));
    for (let i = tr.length - 1; i >= 0; i--) ctx.lineTo(px(tr[i][0]), py(tr[i][1]));
    ctx.closePath(); ctx.fillStyle = 'rgba(41,231,255,0.13)'; ctx.fill();
    ctx.strokeStyle = 'rgba(41,231,255,0.9)'; ctx.lineWidth = 1.6; ctx.lineJoin = 'round';
    ctx.beginPath(); tr.forEach((p, i) => (i ? ctx.lineTo(px(p[0]), py(p[1] + p[2] * U)) : ctx.moveTo(px(p[0]), py(p[1] + p[2] * U)))); ctx.stroke();
    ctx.strokeStyle = 'rgba(255,243,220,0.35)'; ctx.lineWidth = 1;
    ctx.beginPath(); tr.forEach((p, i) => (i ? ctx.lineTo(px(p[0]), py(p[1])) : ctx.moveTo(px(p[0]), py(p[1])))); ctx.stroke();
    for (const [x, y, kind] of game.events || []) this.badge(px(x), py(y) - 8, kind ? K_COLORS.call : K_COLORS.perfect, kind ? 'C' : 'P');
    if (!game.cleared) { const p = tr[tr.length - 1]; this.cross(px(p[0]), py(p[1] + p[2] * U) - 6); }
    ctx.restore();
  }

  badge(x, y, color, letter) {
    const { ctx } = this;
    const r = 6.5;
    ctx.save(); ctx.shadowColor = color; ctx.shadowBlur = 8;
    ctx.fillStyle = color; ctx.beginPath(); ctx.arc(x, y, r, 0, Math.PI * 2); ctx.fill(); ctx.restore();
    ctx.strokeStyle = '#fff'; ctx.lineWidth = 1.1; ctx.beginPath(); ctx.arc(x, y, r, 0, Math.PI * 2); ctx.stroke();
    ctx.fillStyle = '#fff'; ctx.font = '700 9px "Chakra Petch", sans-serif'; ctx.textAlign = 'center'; ctx.textBaseline = 'middle'; ctx.fillText(letter, x, y + 0.5);
  }
  cross(x, y) {
    const { ctx } = this;
    const r = 8;
    ctx.save(); ctx.shadowColor = K_COLORS.crash; ctx.shadowBlur = 12; ctx.strokeStyle = K_COLORS.crash; ctx.lineWidth = 3; ctx.lineCap = 'round';
    ctx.beginPath(); ctx.moveTo(x - r, y - r); ctx.lineTo(x + r, y + r); ctx.moveTo(x + r, y - r); ctx.lineTo(x - r, y + r); ctx.stroke(); ctx.restore();
    ctx.fillStyle = K_COLORS.crash; ctx.font = '700 11px "Chakra Petch", sans-serif'; ctx.textAlign = 'left'; ctx.textBaseline = 'bottom'; ctx.fillText('the bot crashed', x + r + 4, y - r);
  }
  marker(x, y, color, glyph, round = false) {
    const { ctx } = this;
    const px = this.X(x / U), py = this.Y(y) - 12, r = 7;
    if (px < RULER_LEFT || px > this.w) return;
    ctx.fillStyle = color;
    if (round) { ctx.beginPath(); ctx.arc(px, py, r, 0, Math.PI * 2); ctx.fill(); }
    else { ctx.beginPath(); ctx.moveTo(px, py - r); ctx.lineTo(px + r, py + r * 0.8); ctx.lineTo(px - r, py + r * 0.8); ctx.fill(); }
    ctx.fillStyle = round ? '#fff' : '#1a0d00'; ctx.font = '800 9px sans-serif'; ctx.textAlign = 'center'; ctx.textBaseline = 'middle'; ctx.fillText(glyph, px, py + (round ? 0.5 : 1.5));
  }

  band(a, b, color, ants, t) {
    const { ctx, z } = this;
    const c0 = Math.min(a, b), c1 = Math.max(a, b), x = this.X(c0), w = (c1 - c0 + 1) * z;
    ctx.fillStyle = hexA(color, 0.12); ctx.fillRect(x, RULER_TOP, w, this.oy - RULER_TOP);
    ctx.save(); ctx.strokeStyle = color; ctx.lineWidth = 2;
    if (ants) { ctx.setLineDash([6, 4]); ctx.lineDashOffset = -(t * 30) % 10; }
    ctx.strokeRect(x + 1, RULER_TOP + 1, w - 2, this.oy - RULER_TOP - 2); ctx.restore();
  }

  preview(p, look, t) {
    const { ctx, z } = this;
    ctx.save();
    if (p.kind === 'wall') {
      for (const { c, h, kind } of p.cols) {
        ctx.globalAlpha = 0.55;
        for (let j = 0; j < h; j++) this.block(kind === 'hay' || (kind === 'mix' && j === h - 1) ? 'hay' : 'crate', this.X(c), this.Y(j + 1), look, c, j);
        ctx.globalAlpha = 1; ctx.strokeStyle = '#f4ff5a'; ctx.lineWidth = 1.5; ctx.setLineDash([4, 3]);
        ctx.strokeRect(this.X(c) + 0.75, this.Y(Math.max(h, 0.15)) + 0.75, z - 1.5, Math.max(h, 0.15) * z - 1.5);
      }
    } else if (p.kind === 'ceil') {
      ctx.globalAlpha = p.remove ? 1 : 0.7;
      for (let c = Math.min(p.c0, p.c1); c <= Math.max(p.c0, p.c1); c++) {
        if (p.remove) { ctx.strokeStyle = '#ff2b4a'; ctx.lineWidth = 2; ctx.setLineDash([4, 3]); ctx.strokeRect(this.X(c) + 1, this.Y(p.r + 1) + 1, z - 2, z - 2); }
        else if (p.ok(c)) this.barrier(c, p.r, t);
      }
    } else if (p.kind === 'saw') {
      ctx.globalAlpha = 0.7; this.saw(this.X(p.x / U), this.Y(p.h), p.cube, t, false);
      if (!p.ok) { ctx.globalAlpha = 1; this.cross(this.X(p.x / U), this.Y(p.h) - 10); }
    } else if (p.kind === 'disco') {
      ctx.globalAlpha = 0.7; this.disco(this.X(p.x / U), this.Y(p.y / U), t, 0, false);
    } else if (p.kind === 'cols') {
      this.band(p.c0, p.c1, p.remove ? '#ff2b4a' : '#f4ff5a', false, t);
      if (!p.remove) { ctx.strokeStyle = '#f4ff5a'; ctx.lineWidth = 3; line(ctx, this.X(Math.max(p.c0, p.c1) + 1), RULER_TOP, this.X(Math.max(p.c0, p.c1) + 1), this.oy + 6); }
    } else if (p.kind === 'insert') {
      ctx.strokeStyle = '#f4ff5a'; ctx.lineWidth = 3; ctx.setLineDash([6, 4]); ctx.lineDashOffset = -(t * 30) % 10; line(ctx, this.X(p.c), RULER_TOP, this.X(p.c), this.oy + 6);
    } else if (p.kind === 'gauntlet') {
      const color = p.ok ? '#ffc23a' : '#ff2b4a';
      ctx.fillStyle = hexA(color, 0.1); ctx.fillRect(this.X(p.a), RULER_TOP, (p.z - p.a) * z, this.oy - RULER_TOP);
      ctx.strokeStyle = color; ctx.lineWidth = 2; ctx.setLineDash([6, 4]); ctx.lineDashOffset = -(t * 30) % 10;
      ctx.strokeRect(this.X(p.c) + 1, this.Y(p.grid.rows) + 1, p.grid.cols * z - 2, p.grid.rows * z - 2);
      ctx.setLineDash([]); ctx.globalAlpha = 0.6;
      ctx.fillStyle = color; ctx.fillRect(this.X(p.a), this.Y(p.grid.entryRow), (p.c - p.a) * z, p.grid.entryRow * z);
      ctx.fillRect(this.X(p.c + p.grid.cols), this.Y(p.grid.exitRow), (p.z - p.c - p.grid.cols) * z, p.grid.exitRow * z);
    }
    ctx.restore();
  }

  hen(rp, t) { // the bot in the replay: her egg stack under her, running
    const { ctx, z } = this;
    const x = this.X(rp.x / U), base = rp.base / U;
    for (let e = 0; e < rp.eggs; e++) {
      const ey = this.Y(base + e + 0.5);
      ctx.fillStyle = K_COLORS.egg; ctx.strokeStyle = '#29e7ff'; ctx.lineWidth = Math.max(1, z / 12);
      ctx.beginPath(); ctx.ellipse(x, ey, z * 0.36, z * 0.46, 0, 0, Math.PI * 2); ctx.fill(); ctx.stroke();
    }
    const feet = this.Y(base + rp.eggs), bh = z * 1.75, bw = z * 0.9, bob = Math.sin(t * 22) * z * 0.05, cy = feet - bh * 0.5 + bob;
    ctx.save(); ctx.shadowColor = '#fff'; ctx.shadowBlur = 16;
    ctx.fillStyle = '#fbf7ff'; ctx.beginPath(); ctx.ellipse(x, cy, bw * 0.5, bh * 0.48, 0, 0, Math.PI * 2); ctx.fill(); ctx.restore();
    ctx.fillStyle = '#ff2bd6'; ctx.beginPath(); ctx.arc(x + bw * 0.05, cy - bh * 0.48, z * 0.2, 0, Math.PI * 2); ctx.fill();
    ctx.fillStyle = '#ffb43a'; ctx.beginPath(); ctx.moveTo(x + bw * 0.42, cy - bh * 0.22); ctx.lineTo(x + bw * 0.8, cy - bh * 0.15); ctx.lineTo(x + bw * 0.42, cy - bh * 0.06); ctx.fill();
    ctx.fillStyle = '#1a1030'; ctx.beginPath(); ctx.arc(x + bw * 0.2, cy - bh * 0.24, Math.max(1.2, z * 0.08), 0, Math.PI * 2); ctx.fill();
    ctx.strokeStyle = 'rgba(255,255,255,0.8)'; ctx.lineWidth = 1; line(ctx, x, feet + 2, x, this.oy + 6); // (where she is, on the ruler)
  }

  rulers(hover, L) {
    const { ctx, z } = this;
    ctx.fillStyle = '#0b0714'; ctx.fillRect(0, 0, this.w, RULER_TOP); ctx.fillRect(0, 0, RULER_LEFT, this.h);
    ctx.font = '600 10px "Chakra Petch", sans-serif'; ctx.textBaseline = 'middle';
    const perM = z / U;
    const step = [1, 2, 5, 10, 20, 50, 100, 200].find((m) => m * perM >= 56) || 500;
    const m0 = Math.max(0, Math.floor(((RULER_LEFT - this.ox) / perM) / step) * step), m1 = (this.w - this.ox) / perM;
    ctx.textAlign = 'left';
    for (let m = m0; m <= m1; m += step) {
      const x = this.ox + m * perM;
      if (x < RULER_LEFT) continue;
      ctx.fillStyle = 'rgba(233,228,255,0.18)'; ctx.fillRect(x, RULER_TOP - 7, 1, 7);
      ctx.fillStyle = 'rgba(233,228,255,0.55)'; ctx.fillText(`${m} m`, x + 3, RULER_TOP / 2);
      for (let k = 1; k < 5; k++) { const xx = x + (k * step * perM) / 5; ctx.fillStyle = 'rgba(233,228,255,0.12)'; ctx.fillRect(xx, RULER_TOP - 4, 1, 4); }
    }
    if (hover && hover.x >= 0) {
      const x = this.ox + (hover.x / U) * z;
      ctx.fillStyle = '#f4ff5a'; ctx.fillRect(x - 0.5, 0, 1, RULER_TOP);
      const label = `${hover.x.toFixed(1)} m · col ${hover.c}`;
      const w = ctx.measureText(label).width + 10;
      ctx.fillStyle = '#f4ff5a'; roundRect(ctx, Math.min(this.w - w - 4, x + 4), 3, w, RULER_TOP - 6, 4); ctx.fill();
      ctx.fillStyle = '#1a1a00'; ctx.fillText(label, Math.min(this.w - w - 4, x + 4) + 5, RULER_TOP / 2);
    }
    ctx.textAlign = 'right';
    const rstep = z >= 14 ? 1 : z >= 7 ? 5 : 10;
    for (let r = 0; r <= this.rowsUp + 40; r++) {
      const y = this.Y(r + 0.5);
      if (y < RULER_TOP) break;
      if (y > this.h) continue;
      const hot = hover && hover.r === r;
      if (r % rstep === 0 || hot) { ctx.fillStyle = hot ? '#f4ff5a' : 'rgba(233,228,255,0.55)'; ctx.fillText(String(r), RULER_LEFT - 6, y); }
    }
    ctx.fillStyle = '#0b0714'; ctx.fillRect(0, 0, RULER_LEFT, RULER_TOP);
    ctx.strokeStyle = 'rgba(255,255,255,0.08)'; ctx.lineWidth = 1;
    line(ctx, 0, RULER_TOP + 0.5, this.w, RULER_TOP + 0.5); line(ctx, RULER_LEFT + 0.5, 0, RULER_LEFT + 0.5, this.h);
    void L;
  }

  // ---- the minimap: the whole course, the bot's run on it, what's on screen
  minimap(s) {
    const ctx = this.mctx, L = s.L, look = ZONE_LOOK[s.zone] || ZONE_LOOK.city;
    const r = this.mini.getBoundingClientRect();
    const W = r.width, H = r.height;
    ctx.fillStyle = '#05030a'; ctx.fillRect(0, 0, W, H);
    const metres = Math.max(courseLength(L), L.length) + 6;
    const sx = (W - 16) / metres, up = Math.max(16, this.rowsUp), sy = (H - 10) / up;
    const ox = 8, base = H - 5;
    this.miniBox = { ox, sx };
    const X = (m) => ox + m * sx, Y = (rows) => base - rows * sy;
    ctx.fillStyle = hexA(look.accent, 0.08); ctx.fillRect(ox, 3, metres * sx, H - 6);
    const cols = L.cols;
    for (let c = 0; c < cols.length; c++) {
      const k = cols[c];
      if (k.g) continue;
      if (k.h) { ctx.fillStyle = k.kind === 'hay' ? '#8a6a20' : hexA(look.accent, 0.55); ctx.fillRect(X(c * U), Y(k.h), Math.max(1, U * sx), k.h * sy); }
      if (k.ceil) { ctx.fillStyle = '#f4ff5a'; ctx.fillRect(X(c * U), Y(k.ceil + 1), Math.max(1, U * sx), Math.max(1, sy)); }
    }
    for (const sp of spans(L)) {
      const rows = s.grids[sp.name];
      const n = rows ? rows.length : 12;
      ctx.fillStyle = 'rgba(255,194,58,0.22)'; ctx.fillRect(X(sp.c0 * U), Y(n), (sp.c1 - sp.c0) * U * sx, n * sy);
      if (rows) {
        ctx.fillStyle = 'rgba(255,194,58,0.7)';
        for (let i = 0; i < n; i += 1) for (let c = 0; c < rows[i].length; c += 2) if (rows[i][c] !== '.' && rows[i][c] !== undefined) ctx.fillRect(X((sp.c0 + c) * U), Y(n - i), Math.max(1, 2 * U * sx), Math.max(1, sy));
      }
    }
    ctx.fillStyle = '#ff4426'; for (const x of L.saws) ctx.fillRect(X(x) - 1, Y(cols[colAt(x)] && !cols[colAt(x)].g ? cols[colAt(x)].h : 0) - 3, 2.5, 3);
    ctx.fillStyle = '#ffffff'; for (const d of L.discos) ctx.fillRect(X(d.x) - 1, Y(d.y / U) - 1, 2, 2);
    if (s.game && s.game.trace && s.overlays.bot) {
      ctx.strokeStyle = 'rgba(41,231,255,0.9)'; ctx.lineWidth = 1.2; ctx.beginPath();
      s.game.trace.forEach((p, i) => (i ? ctx.lineTo(X(p[0]), Y((p[1] + p[2] * U) / U)) : ctx.moveTo(X(p[0]), Y((p[1] + p[2] * U) / U)))); ctx.stroke();
    }
    ctx.fillStyle = '#f4ff5a'; ctx.fillRect(X(L.length) - 1, 2, 2, H - 4);
    if (s.replay) { ctx.fillStyle = '#fff'; ctx.fillRect(X(s.replay.x) - 1, 2, 2, H - 4); }
    // what's on screen
    const vx = X(((RULER_LEFT - this.ox) / this.z) * U), vw = ((this.w - RULER_LEFT) / this.z) * U * sx;
    ctx.strokeStyle = '#f4ff5a'; ctx.lineWidth = 1.5; ctx.strokeRect(vx, 1.5, vw, H - 3);
    ctx.fillStyle = 'rgba(244,255,90,0.06)'; ctx.fillRect(vx, 1.5, vw, H - 3);
  }
  miniToX(px) { const b = this.miniBox; return b ? (px - b.ox) / b.sx : null; }
}

function line(ctx, x0, y0, x1, y1) { ctx.beginPath(); ctx.moveTo(x0, y0); ctx.lineTo(x1, y1); ctx.stroke(); }
function roundRect(ctx, x, y, w, h, r) { ctx.beginPath(); ctx.moveTo(x + r, y); ctx.arcTo(x + w, y, x + w, y + h, r); ctx.arcTo(x + w, y + h, x, y + h, r); ctx.arcTo(x, y + h, x, y, r); ctx.arcTo(x, y, x + w, y, r); ctx.closePath(); }
function star(ctx, x, y, r, color) {
  ctx.save(); ctx.fillStyle = color; ctx.shadowColor = color; ctx.shadowBlur = 6; ctx.beginPath();
  for (let k = 0; k < 8; k++) { const a = (k * Math.PI) / 4, rr = k % 2 ? r * 0.22 : r; ctx.lineTo(x + Math.cos(a) * rr, y + Math.sin(a) * rr); }
  ctx.fill(); ctx.restore();
}
export function hexA(hex, a) {
  if (hex.startsWith('rgba')) return hex;
  const n = parseInt(hex.slice(1), 16);
  return `rgba(${(n >> 16) & 255},${(n >> 8) & 255},${n & 255},${a})`;
}
