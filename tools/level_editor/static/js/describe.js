// What an edit did, in a few words, and where (the level editor's history), for a level's course or a gauntlet's
// grid, before and after; and what's changed since the save. A course's columns are lined up at least cost, so
// a run copied in reads as what was copied in, not as every column after it. No DOM: tests/history.test.mjs runs it.
import * as K from './course.js';
import * as G from './grid.js';

const U = K.U;
const metres = (x) => `${Math.round(x * 10) / 10} m`;
const plural = (n, one, many = one + 's') => `${n} ${n === 1 ? one : many}`;
const KIND_NAME = { crate: 'crates', hay: 'hay', mix: 'hay-topped crates', bumper: 'bumper', 'gate>': 'gate →', 'gate<': 'gate ←' };
const BOUNCER_NAME = { bumper: 'Bumper wall', 'gate>': 'Gate →', 'gate<': 'Gate ←' };
const bouncerWalls = (L) => K.walls(L).filter((w) => K.isBouncer(w)).length;
const titled = (s) => s.charAt(0).toUpperCase() + s.slice(1).replace(/_/g, ' ');

// ---- lining two sequences up: runs of '=' (the same), '-' (only in a), '+' (only in b); one changed in place: '-' then '+'
const MAX_CELLS = 4e6; // (past this big a middle, the middle is just "replaced")
export function diffSeq(a, b) {
  let pre = 0;
  while (pre < a.length && pre < b.length && a[pre] === b[pre]) pre++;
  let suf = 0;
  while (suf < a.length - pre && suf < b.length - pre && a[a.length - 1 - suf] === b[b.length - 1 - suf]) suf++;
  const A = a.slice(pre, a.length - suf), B = b.slice(pre, b.length - suf);
  const mid = middle(A, B);
  const ops = [];
  const push = (op, n) => { if (!n) return; const last = ops[ops.length - 1]; if (last && last.op === op) last.n += n; else ops.push({ op, n }); };
  push('=', pre);
  for (const o of mid) push(o.op, o.n);
  push('=', suf);
  return ops;
}
// The middle lined up at least cost: the same, free; changed in place, one; put in or taken out, one each. (Changed in
// place costs one, not two, so a column whose saw went reads as that column changed, never as one taken out here and
// another put in there; a run copied in still reads as the run, not every column after it changed)
function middle(A, B) {
  const N = A.length, M = B.length;
  if (!N || !M || N * M > MAX_CELLS) return [{ op: '-', n: N }, { op: '+', n: M }];
  const W = M + 1, cost = new Int32Array((N + 1) * W);
  for (let j = 0; j <= M; j++) cost[j] = j;
  for (let i = 1; i <= N; i++) {
    cost[i * W] = i;
    for (let j = 1; j <= M; j++) {
      const diag = cost[(i - 1) * W + j - 1] + (A[i - 1] === B[j - 1] ? 0 : 1);
      cost[i * W + j] = Math.min(diag, cost[(i - 1) * W + j] + 1, cost[i * W + j - 1] + 1);
    }
  }
  const back = []; // (from the end: lined up first, then taken out, then put in)
  let i = N, j = M;
  while (i > 0 || j > 0) {
    const c = cost[i * W + j];
    if (i > 0 && j > 0 && c === cost[(i - 1) * W + j - 1] + (A[i - 1] === B[j - 1] ? 0 : 1)) { if (A[i - 1] === B[j - 1]) back.push('='); else back.push('+', '-'); i--; j--; }
    else if (i > 0 && c === cost[(i - 1) * W + j] + 1) { back.push('-'); i--; }
    else { back.push('+'); j--; }
  }
  const ops = [];
  for (let k = back.length - 1; k >= 0; k--) { const last = ops[ops.length - 1]; if (last && last.op === back[k]) last.n++; else ops.push({ op: back[k], n: 1 }); }
  return ops;
}

// ---- a course against another (the saved one, or the one before an edit)
const sig = (k) => (k.g ? 'g:' + k.g : `${k.h}|${k.kind}|${k.ceil}|${k.top || 0}`);
function sigs(L) { // (a column's saws, disco balls, lasers, lava and springs in it too: in a long flat run they're what tells columns apart)
  const extra = new Map();
  const add = (x, s) => { const c = K.colAt(x); extra.set(c, (extra.get(c) || '') + s); };
  for (const x of L.saws) add(x, `s${Math.round((x - K.colAt(x) * U) * 100)}`);
  for (const d of L.discos) add(d.x, `d${Math.round((d.x - K.colAt(d.x) * U) * 100)}:${Math.round(d.y * 100)}`);
  for (const z of L.lasers || []) add((K.laserCol(z) + 0.5) * U, `z${z.dir}:${z.row}`); // (in the column it's set into)
  for (const f of L.lava || []) add(f.x, `a${Math.round((f.x - K.colAt(f.x) * U) * 100)}:${f.len}`);
  for (const p of L.springs || []) add(p.x, `j${Math.round((p.x - K.colAt(p.x) * U) * 100)}:${p.power}`);
  return L.cols.map((k, c) => sig(k) + (extra.get(c) || ''));
}
export function diffCourse(was, now) {
  const ops = diffSeq(sigs(was), sigs(now));
  const map = new Array(was.cols.length).fill(-1); // a saved column: where it is now (deleted ones: where they were cut)
  const changed = new Set(), replaced = [], inserted = [], removed = [];
  let a = 0, b = 0;
  for (let i = 0; i < ops.length; i++) {
    const o = ops[i];
    if (o.op === '=') { for (let k = 0; k < o.n; k++) map[a + k] = b + k; a += o.n; b += o.n; continue; }
    // a run of - and + together: as many as both have, replaced in place; the rest put in or taken out
    let del = 0, ins = 0;
    while (i < ops.length && ops[i].op !== '=') { if (ops[i].op === '-') del += ops[i].n; else ins += ops[i].n; i++; }
    i--;
    const both = Math.min(del, ins);
    for (let k = 0; k < both; k++) { // (lined up: changed if its wall is, not just what's on it)
      map[a + k] = b + k;
      if (sig(was.cols[a + k]) === sig(now.cols[b + k])) continue;
      changed.add(b + k);
      const last = replaced[replaced.length - 1];
      if (last && last.c1 === b + k - 1) { last.c1++; last.was.push(was.cols[a + k]); } else replaced.push({ c0: b + k, c1: b + k, was: [was.cols[a + k]] });
    }
    if (ins > both) { inserted.push({ c0: b + both, c1: b + ins - 1 }); for (let k = both; k < ins; k++) changed.add(b + k); }
    if (del > both) { removed.push({ at: b + ins, n: del - both, was: was.cols.slice(a + both, a + del) }); for (let k = both; k < del; k++) map[a + k] = b + ins; }
    a += del; b += ins;
  }
  const mapX = (x) => { const c = Math.max(0, Math.min(was.cols.length - 1, K.colAt(x))); return map[c] < 0 ? x : map[c] * U + (x - c * U); };
  const near = (p, q, tol) => Math.abs(p - q) < tol;
  const sawsLeft = now.saws.slice(), sawsGone = [];
  for (const x of was.saws) {
    const at = mapX(x), i = sawsLeft.findIndex((y) => near(y, at, 0.05));
    if (i >= 0) sawsLeft.splice(i, 1); else sawsGone.push(at);
  }
  const discosLeft = now.discos.slice(), discosGone = [];
  for (const d of was.discos) {
    const at = mapX(d.x), i = discosLeft.findIndex((e) => near(e.x, at, 0.05) && near(e.y, d.y, 0.05));
    if (i >= 0) discosLeft.splice(i, 1); else discosGone.push({ x: at, y: d.y });
  }
  const follow = (was, now, same, place = (p) => mapX(p.x)) => { // (each one saved, followed to where it is now: kept, or gone)
    const left = now.slice(), gone = [];
    for (const p of was) { const at = { ...p, x: place(p) }, i = left.findIndex((q) => near(q.x, at.x, 0.05) && same(p, q)); if (i >= 0) left.splice(i, 1); else gone.push(at); }
    return { added: left, removed: gone };
  };
  const finishWas = mapX(Math.min(was.length, Math.max(0, was.cols.length * U - 1e-6)));
  return {
    changed, replaced, inserted, removed, mapX,
    saws: { added: sawsLeft, removed: sawsGone },
    discos: { added: discosLeft, removed: discosGone },
    lasers: follow(was.lasers || [], now.lasers || [], (p, q) => p.row === q.row && p.dir === q.dir, (p) => mapX((K.laserCol(p) + 0.5) * U) + (p.dir ? 0.5 : -0.5) * U), // (with the column it's set into)
    lava: follow(was.lava || [], now.lava || [], (p, q) => p.len === q.len),
    springs: follow(was.springs || [], now.springs || [], (p, q) => p.power === q.power),
    bouncers: [bouncerWalls(was), bouncerWalls(now)], // (bumper and gate walls, saved and now)
    finish: Math.abs(was.length - now.length) > 1e-9 ? { from: was.length, to: now.length, ghost: finishWas } : null,
    difficulty: was.diffOffset !== now.diffOffset || was.diffScale !== now.diffScale ? [[was.diffOffset, was.diffScale], [now.diffOffset, now.diffScale]] : null,
    raids: String(was.raids) !== String(now.raids) ? [was.raids, now.raids] : null,
    notes: was.notes.join('\n') !== now.notes.join('\n'),
    gauntlets: { was: K.spans(was), now: K.spans(now) },
  };
}

// what's changed since the save, in a few words
export function summarizeCourse(d) {
  const out = [];
  const gw = d.gauntlets.was.map((s) => s.name).join(), gn = d.gauntlets.now.map((s) => s.name).join();
  if (gw !== gn) out.push(gn ? `its gauntlet now the ${titled(gn)}` : `its gauntlet (the ${titled(gw)}) taken out`);
  const ins = d.inserted.reduce((n, r) => n + r.c1 - r.c0 + 1, 0), rem = d.removed.reduce((n, r) => n + r.n, 0);
  const mod = d.replaced.reduce((n, r) => n + r.c1 - r.c0 + 1, 0);
  if (mod) out.push(`${plural(mod, 'column')} changed`);
  if (ins) out.push(`${plural(ins, 'column')} put in`);
  if (rem) out.push(`${plural(rem, 'column')} taken out`);
  const sa = d.saws.added.length, sr = d.saws.removed.length;
  if (sa || sr) out.push([sa ? plural(sa, 'saw') + ' added' : '', sr ? plural(sr, 'saw') + ' removed' : ''].filter(Boolean).join(', '));
  const da = d.discos.added.length, dr = d.discos.removed.length;
  if (da || dr) out.push([da ? plural(da, 'disco ball') + ' added' : '', dr ? plural(dr, 'disco ball') + ' removed' : ''].filter(Boolean).join(', '));
  const za = d.lasers.added.length, zr = d.lasers.removed.length;
  if (za || zr) out.push([za ? plural(za, 'laser') + ' added' : '', zr ? plural(zr, 'laser') + ' removed' : ''].filter(Boolean).join(', '));
  const aa = d.lava.added.length, ar = d.lava.removed.length;
  if (aa || ar) out.push([aa ? plural(aa, 'lava run') + ' added' : '', ar ? plural(ar, 'lava run') + ' removed' : ''].filter(Boolean).join(', '));
  const ja = d.springs.added.length, jr = d.springs.removed.length;
  if (ja || jr) out.push([ja ? plural(ja, 'spring') + ' added' : '', jr ? plural(jr, 'spring') + ' removed' : ''].filter(Boolean).join(', '));
  if (d.bouncers && d.bouncers[0] !== d.bouncers[1]) out.push(`bouncer walls ${d.bouncers[0]} → ${d.bouncers[1]}`);
  if (d.finish) out.push(`the finish ${d.finish.to > d.finish.from ? '+' : '−'}${metres(Math.abs(d.finish.to - d.finish.from))}`);
  if (d.difficulty) out.push('its difficulty');
  if (d.raids) out.push('its raids');
  if (d.notes) out.push('its description');
  return out.join(', ');
}

// where a course's changes are, metres: [x0, x1], or null
export function courseFocus(d) {
  let x0 = Infinity, x1 = -Infinity;
  const take = (a, b = a) => { x0 = Math.min(x0, a); x1 = Math.max(x1, b); };
  for (const r of d.replaced) take(r.c0 * U, (r.c1 + 1) * U);
  for (const r of d.inserted) take(r.c0 * U, (r.c1 + 1) * U);
  for (const r of d.removed) take(r.at * U);
  for (const x of [...d.saws.added, ...d.saws.removed]) take(x);
  for (const p of [...d.discos.added, ...d.discos.removed]) take(p.x);
  for (const p of [...d.lasers.added, ...d.lasers.removed]) take(p.x);
  for (const p of [...d.lava.added, ...d.lava.removed]) take(p.x, p.x + p.len * U);
  for (const p of [...d.springs.added, ...d.springs.removed]) take(p.x);
  if (d.finish) take(Math.min(d.finish.ghost, d.finish.to), Math.max(d.finish.ghost, d.finish.to));
  return x0 <= x1 ? [x0, x1] : null;
}

const colsAt = (c0, c1) => (c0 === c1 ? `1 column at ${metres(c0 * U)}` : `${c1 - c0 + 1} columns at ${metres(c0 * U)}–${metres((c1 + 1) * U)}`);
function changedRun(d) { // (the columns an edit changed in place, first to last)
  const cs = [...d.changed].sort((p, q) => p - q);
  return cs.length ? [cs[0], cs[cs.length - 1], cs] : null;
}

// What an edit to a course did, from the tool's kind and the course before and after: { label, focus: [x0, x1] }.
// hint: { c0, c1 }, the columns the tool worked on (copied in, taken out: in a flat run, a diff can't tell which)
export function describeLevel(kind, was, now, hint = null) {
  const d = diffCourse(was, now);
  let focus = courseFocus(d);
  if (hint && hint.c0 !== undefined) focus = [hint.c0 * U, (hint.c1 + 1) * U];
  const run = changedRun(d);
  const label = (() => {
    switch (kind) {
      case 'walls': case 'bouncer': {
        if (!run) break;
        const hs = new Set(run[2].map((c) => now.cols[c].h)), ks = new Set(run[2].map((c) => now.cols[c].kind));
        const h = [...hs][0], b = BOUNCER_NAME[[...ks][0]];
        if (b && hs.size === 1 && ks.size === 1) return `${b} ${run[1] - run[0] + 1} × ${h} at ${metres(run[0] * U)}`; // (a bumper's or a gate's: cells × high)
        return `${hs.size === 1 ? (h ? `Walls ${h} high` : 'Walls down to the street') : 'Walls reshaped'} · ${colsAt(run[0], run[1])}`;
      }
      case 'kind': if (run) return `${titled(KIND_NAME[now.cols[run[0]].kind] || now.cols[run[0]].kind)} · ${colsAt(run[0], run[1])}`; break;
      case 'ceil': if (run) return `Barrier at ${now.cols[run[0]].ceil} · ${colsAt(run[0], run[1])}`; break;
      case 'ceil-off': if (run) return `Barriers off · ${colsAt(run[0], run[1])}`; break;
      case 'block': if (run) { const k = now.cols[run[0]]; return `Floating block ${k.ceil}–${k.top} · ${colsAt(run[0], run[1])}`; } break;
      case 'block-off': if (run) return `Floating blocks off · ${colsAt(run[0], run[1])}`; break;
      case 'saw-add': if (d.saws.added.length) return `Saw added at ${metres(d.saws.added[0])}`; break;
      case 'saw-remove': if (d.saws.removed.length) return `Saw removed at ${metres(d.saws.removed[0])}`; break;
      case 'saw-move': if (d.saws.added.length && d.saws.removed.length) return `Saw moved ${metres(d.saws.removed[0])} → ${metres(d.saws.added[0])}`; break;
      case 'disco-add': if (d.discos.added.length) return `Disco ball added at ${metres(d.discos.added[0].x)}, ${metres(d.discos.added[0].y)} up`; break;
      case 'disco-remove': if (d.discos.removed.length) return `Disco ball removed at ${metres(d.discos.removed[0].x)}`; break;
      case 'disco-move': if (d.discos.added.length) return `Disco ball moved to ${metres(d.discos.added[0].x)}, ${metres(d.discos.added[0].y)} up`; break;
      case 'laser-add': if (d.lasers.added.length) return `Laser added at ${metres(d.lasers.added[0].x)}, row ${d.lasers.added[0].row}, firing ${d.lasers.added[0].dir ? 'right' : 'left'}`; break;
      case 'laser-remove': if (d.lasers.removed.length) return `Laser removed at ${metres(d.lasers.removed[0].x)}`; break;
      case 'laser-move': if (d.lasers.added.length) return `Laser moved to ${metres(d.lasers.added[0].x)}, row ${d.lasers.added[0].row}, firing ${d.lasers.added[0].dir ? 'right' : 'left'}`; break;
      case 'lava-add': if (d.lava.added.length) return `Lava added at ${metres(d.lava.added[0].x)}, ${plural(d.lava.added[0].len, 'cell')}`; break;
      case 'lava-remove': if (d.lava.removed.length) return `Lava removed at ${metres(d.lava.removed[0].x)}`; break;
      case 'lava-move': if (d.lava.added.length) return `Lava moved to ${metres(d.lava.added[0].x)}, ${plural(d.lava.added[0].len, 'cell')}`; break;
      case 'spring-add': if (d.springs.added.length) return `Put in a Spring at ${metres(d.springs.added[0].x)}`; break;
      case 'spring-remove': if (d.springs.removed.length) return `Spring taken out at ${metres(d.springs.removed[0].x)}`; break;
      case 'spring-move': if (d.springs.added.length && d.springs.removed.length) return `Spring moved ${metres(d.springs.removed[0].x)} → ${metres(d.springs.added[0].x)}`; break;
      case 'spring-lift': if (d.springs.added.length) return `Spring: Lift ${d.springs.added[0].power}`; break;
      case 'cols-in': case 'paste': {
        const n = hint && hint.c0 !== undefined ? hint.c1 - hint.c0 + 1 : d.inserted.reduce((t, r) => t + r.c1 - r.c0 + 1, 0);
        const at = hint && hint.c0 !== undefined ? hint.c0 : d.inserted.length ? d.inserted[0].c0 : -1;
        if (n && at >= 0) return `${plural(n, 'column')} ${kind === 'paste' ? 'put' : 'copied'} in at ${metres(at * U)}`;
        break;
      }
      case 'cols-out': case 'cut': {
        const n = hint && hint.c0 !== undefined ? hint.c1 - hint.c0 + 1 : d.removed.reduce((t, r) => t + r.n, 0);
        const at = hint && hint.c0 !== undefined ? hint.c0 : d.removed.length ? d.removed[0].at : -1;
        if (n && at >= 0) return `${plural(n, 'column')} ${kind === 'cut' ? 'cut' : 'taken out'} at ${metres(at * U)}`;
        break;
      }
      case 'gauntlet': {
        const w = d.gauntlets.was[0], n = d.gauntlets.now[0];
        if (n && (!w || w.name !== n.name)) return `The ${titled(n.name)} put in at ${metres(n.c0 * U)}`;
        if (w && !n) return `The ${titled(w.name)} taken out`;
        if (n) return `The ${titled(n.name)} moved to ${metres(n.c0 * U)}`;
        break;
      }
      case 'finish': if (d.finish) return `Finish line ${metres(d.finish.from)} → ${metres(d.finish.to)}`; break;
      case 'difficulty': if (d.difficulty) { const [[o0, s0], [o1, s1]] = d.difficulty; return `Difficulty ${o0 ?? 'its own'} + ${s0} → ${o1} + ${s1}`; } break;
      case 'raids': if (d.raids) return `Raids ${d.raids[0]} → ${d.raids[1]}`; break;
      case 'notes': return 'Description edited';
      case 'revert': return 'Back to the save';
      default: break;
    }
    const sum = summarizeCourse(d);
    return sum ? titled(sum) : 'Edited';
  })();
  return { label, focus };
}

// ---- a grid against another: cells changed (the same size), or columns and rows put in or taken out
const colStrings = (rows) => Array.from({ length: G.cols(rows) }, (_, c) => rows.map((r) => r[c]).join(''));
export function diffGrid(was, now) {
  const out = { cells: new Map(), size: null, colsIn: [], colsOut: [], rowsIn: [], rowsOut: [] }; // cells: "c,i" -> what it was
  if (was.length === now.length && G.cols(was) === G.cols(now)) {
    for (let i = 0; i < now.length; i++) if (was[i] !== now[i]) for (let c = 0; c < now[i].length; c++) if (was[i][c] !== now[i][c]) out.cells.set(c + ',' + i, was[i][c]);
    return out;
  }
  out.size = { was: [G.cols(was), was.length], now: [G.cols(now), now.length] };
  const lineUp = (a, b, into, outOf) => { // (columns or rows: those put in, those taken out; the rest compared cell by cell)
    let x = 0, y = 0;
    const pairs = [];
    for (const o of diffSeq(a, b)) {
      if (o.op === '=') { for (let k = 0; k < o.n; k++) pairs.push([x + k, y + k]); x += o.n; y += o.n; }
      else if (o.op === '-') { outOf.push({ at: y, n: o.n }); x += o.n; }
      else { into.push({ from: y, n: o.n }); y += o.n; }
    }
    return pairs;
  };
  if (was.length === now.length) lineUp(colStrings(was), colStrings(now), out.colsIn, out.colsOut);
  else if (G.cols(was) === G.cols(now)) lineUp(was, now, out.rowsIn, out.rowsOut);
  return out;
}
const CELL_NAME = Object.fromEntries(G.CELLS.map((c) => [c.ch, c.name.toLowerCase()]));
export const cellName = (ch) => CELL_NAME[ch] || ch;
export function summarizeGrid(d) {
  if (d.size) {
    const parts = [];
    for (const r of d.colsIn) parts.push(`${plural(r.n, 'column')} put in`);
    for (const r of d.colsOut) parts.push(`${plural(r.n, 'column')} taken out`);
    for (const r of d.rowsIn) parts.push(`${plural(r.n, 'row')} put in`);
    for (const r of d.rowsOut) parts.push(`${plural(r.n, 'row')} taken out`);
    return parts.length ? parts.join(', ') : `its size (${d.size.now[0]} × ${d.size.now[1]}, ${d.size.was[0]} × ${d.size.was[1]} before)`;
  }
  return d.cells.size ? `${plural(d.cells.size, 'cell')} changed` : '';
}
// where a grid's changes are: { c0, i0, c1, i1 } (cells, rows top first), or null
export function gridFocus(d, now) {
  if (d.size) {
    const r = d.colsIn[0] || d.colsOut[0], q = d.rowsIn[0] || d.rowsOut[0];
    if (r) return { c0: r.from ?? r.at, i0: 0, c1: (r.from ?? r.at) + (r.from !== undefined ? r.n - 1 : 0), i1: now.length - 1 };
    if (q) return { c0: 0, i0: q.from ?? q.at, c1: G.cols(now) - 1, i1: (q.from ?? q.at) + (q.from !== undefined ? q.n - 1 : 0) };
    return null;
  }
  let c0 = Infinity, i0 = Infinity, c1 = -1, i1 = -1;
  for (const key of d.cells.keys()) { const [c, i] = key.split(',').map(Number); c0 = Math.min(c0, c); c1 = Math.max(c1, c); i0 = Math.min(i0, i); i1 = Math.max(i1, i); }
  return c1 >= 0 ? { c0, i0, c1, i1 } : null;
}

// What an edit to a grid did: { label, focus }. hint: { c0, c1 } or { i0, i1 }, the columns or rows the tool worked on
export function describeGrid(kind, was, now, hint = null) {
  const d = diffGrid(was, now);
  let focus = gridFocus(d, now);
  if (hint && hint.c0 !== undefined && hint.i0 === undefined) focus = { c0: hint.c0, i0: 0, c1: hint.c1, i1: now.length - 1 };
  if (hint && hint.i0 !== undefined && hint.c0 === undefined) focus = { c0: 0, i0: hint.i0, c1: G.cols(now) - 1, i1: hint.i1 };
  const label = (() => {
    const n = d.cells.size;
    const to = () => { const kinds = new Set(); for (const key of d.cells.keys()) { const [c, i] = key.split(',').map(Number); kinds.add(now[i][c]); } return kinds.size === 1 ? ` to ${cellName([...kinds][0])}` : ''; };
    const box = focus && !d.size ? `${focus.c1 - focus.c0 + 1} × ${focus.i1 - focus.i0 + 1}` : '';
    switch (kind) {
      case 'brush': if (n) return `Brush: ${plural(n, 'cell')}${to()}`; break;
      case 'rect': if (n) return `Rect: ${box}${to()}`; break;
      case 'fill': if (n) return `Filled a ${box} block${to()}`; break;
      case 'door': {
        for (const ch of ['>', 'X']) {
          const a = G.findDoor(was, ch), b = G.findDoor(now, ch);
          if (a && b && a.i !== b.i) return `${ch === '>' ? 'Entry' : 'Exit'} door to row ${G.gameRow(now, b.i)}`;
        }
        break;
      }
      case 'cols-in': case 'cols-out': {
        const n = hint && hint.c0 !== undefined ? hint.c1 - hint.c0 + 1 : (d.colsIn[0] || d.colsOut[0] || {}).n;
        const at = hint && hint.c0 !== undefined ? hint.c0 : (d.colsIn[0] || {}).from ?? (d.colsOut[0] || {}).at;
        if (n) return `${plural(n, 'column')} ${kind === 'cols-in' ? 'copied in' : 'taken out'} at col ${at}`;
        break;
      }
      case 'rows-in': case 'rows-out': {
        const n = hint && hint.i0 !== undefined ? hint.i1 - hint.i0 + 1 : (d.rowsIn[0] || d.rowsOut[0] || {}).n;
        if (n) return `${plural(n, 'row')} ${kind === 'rows-in' ? 'copied in' : 'taken out'}${hint && hint.i0 !== undefined ? ` at row ${G.gameRow(was, hint.i1)}` : ''}`;
        break;
      }
      case 'move': if (n) return `Moved a block (${plural(n, 'cell')} changed)`; break;
      case 'paste': if (n) return `Pasted a ${box} block`; break;
      case 'flip': if (n) return `Flipped a ${box} block`; break;
      case 'cut': if (n) return `Cut a ${box} block`; break;
      case 'notes': return 'Description edited';
      case 'revert': return 'Back to the save';
      default: break;
    }
    const sum = summarizeGrid(d);
    return sum ? titled(sum) : 'Edited';
  })();
  return { label, focus };
}
