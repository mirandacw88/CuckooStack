// The level editor's history: each level's and each grid's states one after another, each with the edit that made it
// (what it did in a few words, when, where), so undo, redo and a jump to any edit are moves along it; edits undone stay
// till a new edit replaces them; the saved state is marked. Revert is an edit too ("Back to the save"), so what came
// before it stays to go back to. Kept in this browser (IndexedDB), so a reload brings it back. The timeline is plain
// data (tests/history.test.mjs runs it in Node); its storage is a no-op where there's no IndexedDB.

export const CAP = 200; // states kept per level or grid (the oldest go first)

// a timeline starting at a state (as saved, or as opened)
export function timeline(state, label = 'As saved', at = Date.now()) {
  return { states: [state], entries: [{ label, at, focus: null, kind: 'start' }], cursor: 0, saved: 0 };
}
export const current = (t) => t.states[t.cursor];
export const canUndo = (t) => t.cursor > 0;
export const canRedo = (t) => t.cursor < t.states.length - 1;
export const nextUndo = (t) => (canUndo(t) ? t.entries[t.cursor] : null);   // (the edit undo takes back)
export const nextRedo = (t) => (canRedo(t) ? t.entries[t.cursor + 1] : null); // (the edit redo makes again)

// an edit: what's after the cursor goes, the new state goes on (nothing changed: the same timeline)
export function record(t, state, label, focus = null, kind = 'edit', at = Date.now()) {
  if (state === t.states[t.cursor]) return t;
  const states = t.states.slice(0, t.cursor + 1), entries = t.entries.slice(0, t.cursor + 1);
  states.push(state);
  entries.push({ label, at, focus, kind });
  let saved = t.saved > t.cursor ? -1 : t.saved; // (the saved state was undone past, and is gone now)
  const drop = Math.max(0, states.length - CAP);
  if (drop) { states.splice(0, drop); entries.splice(0, drop); saved = saved - drop; if (saved < 0) saved = -1; }
  return { states, entries, cursor: states.length - 1, saved };
}
export function jump(t, i) { return i >= 0 && i < t.states.length && i !== t.cursor ? { ...t, cursor: i } : t; }
export const undo = (t) => jump(t, t.cursor - 1);
export const redo = (t) => jump(t, t.cursor + 1);
export const markSaved = (t) => ({ ...t, saved: t.cursor });

// the edits since the save, newest first (all of them back to the start if the saved state isn't kept, or isn't on the
// way to here)
export function sinceSaved(t) {
  const from = t.saved >= 0 && t.saved <= t.cursor ? t.saved + 1 : 1;
  const out = [];
  for (let i = t.cursor; i >= from; i--) out.push({ ...t.entries[i], index: i });
  return out;
}

// how long ago, in a word or two
export function ago(at, now = Date.now()) {
  const s = Math.max(0, Math.round((now - at) / 1000));
  if (s < 10) return 'now';
  if (s < 60) return `${s} s`;
  const m = Math.round(s / 60);
  if (m < 60) return `${m} min`;
  const h = Math.round(m / 60);
  if (h < 24) return `${h} h`;
  return `${Math.round(h / 24)} d`;
}

// ---- kept in this browser: IndexedDB, keyed "level:13", "gauntlet:tomb" ({ base: the saved text it grew from, t })
const DB = 'cuckoo-level-editor', STORE = 'history';
let opening = null;
function db() {
  if (typeof indexedDB === 'undefined') return Promise.resolve(null);
  if (!opening) {
    opening = new Promise((resolve) => {
      try {
        const r = indexedDB.open(DB, 1);
        r.onupgradeneeded = () => r.result.createObjectStore(STORE);
        r.onsuccess = () => resolve(r.result);
        r.onerror = () => resolve(null);
        r.onblocked = () => resolve(null);
      } catch { resolve(null); }
    });
  }
  return opening;
}
// every one kept: Map id -> { base, t }
export async function loadAll() {
  const d = await db();
  const out = new Map();
  if (!d) return out;
  return new Promise((resolve) => {
    try {
      const tx = d.transaction(STORE), st = tx.objectStore(STORE);
      const keys = st.getAllKeys(), values = st.getAll();
      tx.oncomplete = () => { keys.result.forEach((k, i) => out.set(k, values.result[i])); resolve(out); };
      tx.onerror = () => resolve(out);
    } catch { resolve(out); }
  });
}
const pending = new Map();
export function keep(id, value) { // (a moment after the last edit)
  clearTimeout(pending.get(id));
  pending.set(id, setTimeout(async () => {
    pending.delete(id);
    const d = await db();
    if (!d) return;
    try { const tx = d.transaction(STORE, 'readwrite'); if (value) tx.objectStore(STORE).put(value, id); else tx.objectStore(STORE).delete(id); } catch { /* (full, or blocked: it's a convenience) */ }
  }, 500));
}
