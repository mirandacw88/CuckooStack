// The level editor's dialogs, drawn in the page: the browser's own confirm() and prompt() don't show in every browser
// (the Claude app's browser pane among them: there they're answered "no" unseen), and these look like the rest of the
// editor. One at a time; while one's up, the editor's keys wait.
import { el, esc } from './util.js';

let current = null;

// A dialog: its title, what it says (HTML, or an element), its buttons ({ label, value, kind: 'primary' | 'danger',
// default: Enter's }). Resolves to the button's value, or null (Escape, Cancel, a click outside it)
export function modal({ title, body, actions, wide = false, onOpen = null }) {
  if (current) current.close(null);
  return new Promise((resolve) => {
    const back = el('div', 'modal-back');
    const box = el('div', 'modal' + (wide ? ' wide' : ''));
    box.setAttribute('role', 'dialog');
    box.setAttribute('aria-modal', 'true');
    box.setAttribute('aria-label', title);
    box.append(el('h2', null, esc(title)));
    const content = el('div', 'modal-body');
    if (typeof body === 'string') content.innerHTML = body;
    else if (body) content.append(body);
    const bar = el('div', 'modal-actions');
    const buttons = actions.map((a) => {
      const b = el('button', a.kind || 'ghost', esc(a.label));
      b.onclick = () => close(a.value === undefined ? null : a.value);
      bar.append(b);
      return b;
    });
    box.append(content, bar);
    back.append(box);
    document.body.append(back);
    requestAnimationFrame(() => back.classList.add('in'));
    const key = (e) => {
      e.stopPropagation(); // (the editor's keys wait)
      if (e.key === 'Escape') { e.preventDefault(); close(null); return; }
      if (e.key === 'Enter' && !(e.target.closest && e.target.closest('textarea'))) {
        const i = actions.findIndex((a) => a.default);
        if (i >= 0 && !buttons[i].disabled) { e.preventDefault(); buttons[i].click(); }
      }
    };
    function close(value) {
      if (!back.isConnected || back.classList.contains('out')) return;
      document.removeEventListener('keydown', key, true);
      back.classList.remove('in');
      back.classList.add('out');
      setTimeout(() => back.remove(), 200);
      current = null;
      resolve(value);
    }
    document.addEventListener('keydown', key, true);
    back.addEventListener('pointerdown', (e) => { if (e.target === back) close(null); });
    current = { close, buttons, content };
    const first = buttons[actions.findIndex((a) => a.default)] || buttons[buttons.length - 1];
    setTimeout(() => first && first.focus(), 30);
    if (onOpen) onOpen({ buttons, content, close });
  });
}

// yes or no: true if its button was pressed
export async function ask({ title, body, ok = 'OK', cancel = 'Cancel', danger = false }) {
  return (await modal({ title, body, actions: [{ label: cancel }, { label: ok, value: true, kind: danger ? 'danger' : 'primary', default: true }] })) === true;
}

// a line of text, checked as it's typed (check: why it won't do, or nothing); null if cancelled
export async function askText({ title, body = '', value = '', placeholder = '', ok = 'OK', check = () => null }) {
  const wrap = el('div');
  if (body) wrap.append(el('p', null, esc(body)));
  const input = el('input', 'modal-input');
  input.value = value;
  input.placeholder = placeholder;
  input.spellcheck = false;
  input.autocomplete = 'off';
  const why = el('div', 'modal-why');
  wrap.append(input, why);
  const v = await modal({ title, body: wrap, actions: [{ label: 'Cancel' }, { label: ok, value: 'ok', kind: 'primary', default: true }],
    onOpen: ({ buttons }) => {
      const update = () => { const problem = check(input.value.trim()); why.textContent = problem || ''; buttons[1].disabled = !!problem; };
      input.addEventListener('input', update);
      update();
      setTimeout(() => { input.focus(); input.select(); }, 40);
    } });
  return v === 'ok' ? input.value.trim() : null;
}
