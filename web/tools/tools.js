/* Controller tools index: D-pad moves between tiles, Cross opens, Circle goes back. GPL-3.0-or-later. */
(() => {
  'use strict';
  const tiles = () => Array.from(document.querySelectorAll('.tool'));
  function focusAt(i) { const t = tiles(); if (t.length) t[(i + t.length) % t.length].focus(); }
  function move(dir) {
    const t = tiles(), cur = t.indexOf(document.activeElement);
    if (cur < 0) { focusAt(0); return true; }
    const r = t[cur].getBoundingClientRect(), cx = r.x + r.width / 2, cy = r.y + r.height / 2;
    const horiz = dir === 'left' || dir === 'right', sign = (dir === 'left' || dir === 'up') ? -1 : 1;
    const best = t.filter((e, i) => i !== cur).map(e => { const b = e.getBoundingClientRect(); const dx = b.x + b.width / 2 - cx, dy = b.y + b.height / 2 - cy;
      return { e, along: (horiz ? dx : dy) * sign, off: Math.abs(horiz ? dy : dx) }; }).filter(x => x.along > 4).sort((a, b) => (a.along + a.off * 3) - (b.along + b.off * 3))[0];
    if (best) best.e.focus();
    return true;
  }
  function controller(a) {
    if (['up', 'down', 'left', 'right'].includes(a)) return move(a);
    if (a === 'cross' && document.activeElement && document.activeElement.classList.contains('tool')) { document.activeElement.click(); return true; }
    if (a === 'circle') { location.href = '/'; return true; }
    return false;
  }
  window.omniPadController = controller;
  document.addEventListener('keydown', e => {
    const a = { ArrowUp: 'up', ArrowDown: 'down', ArrowLeft: 'left', ArrowRight: 'right', Escape: 'circle' }[e.key];
    if (a && controller(a)) e.preventDefault();
  });
  let glyphs = 'xbox';
  try { glyphs = localStorage.getItem('ob.glyphs') === 'playstation' ? 'playstation' : 'xbox'; } catch (_) { /* default */ }
  document.getElementById('hint').textContent = glyphs === 'xbox' ? 'D-pad · Move   A · Select   B · Back' : 'D-pad · Move   × · Select   ○ · Back';
  focusAt(0);
})();
