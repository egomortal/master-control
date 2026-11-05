/* Master Control 0.4.4: the in-page host for the Tools pages (Astra's R3-049/052/053/054).
 * Those pages talk to `window.omnibloomHost.request({version,id,method,params})`. Inside Master
 * Control they are served from Master Control's own origin, so this adapter answers them with the
 * same-origin HTTP API the main menu already uses. It adds no new native capability:
 *   mc.state           GET  /api/state            (controller addresses are dropped before the page sees them)
 *   mc.rumble          POST /api/rumble?slot=N    (Bluetooth only, fixed 500 ms pulse, as the menu)
 *   mc.remap.apply     POST /api/remap?dev=V_P    (body = remap file text, validated natively)
 *   mc.profile.apply   POST /api/remap?dev=V_P    (the page has already resolved the profile name to a map)
 * Anything else answers UNSUPPORTED, so a page shows its honest "not available" state.
 * GPL-3.0-or-later. */
(() => {
  'use strict';
  const MUTATING_HEADERS = { 'X-PadBridge': '1', 'Content-Type': 'text/plain' };
  const DEV = /^[0-9A-Fa-f]{4}_[0-9A-Fa-f]{4}$/;
  const fail = (id, code) => ({ id, ok: false, code });
  const okay = (id, result) => ({ id, ok: true, result });

  async function post(path, body) {
    const r = await fetch(path, { method: 'POST', headers: MUTATING_HEADERS, body: body || '', cache: 'no-store' });
    let data = null;
    try { data = await r.json(); } catch (_) { /* empty */ }
    return { status: r.status, data };
  }

  async function state() {
    const r = await fetch('/api/state', { cache: 'no-store' });
    if (!r.ok) throw Error('HOST_UNAVAILABLE');
    const s = await r.json();
    const pads = (Array.isArray(s.pads) ? s.pads : []).slice(0, 16).map(p => ({
      slot: p.slot, transport: p.transport, model: p.model, profile: p.profile,
      vid: p.vid, pid: p.pid, battery: p.battery, charging: p.charging === true,
      buttons: Array.isArray(p.buttons) ? p.buttons.slice(0, 32) : []
    }));
    return { pads, bluetooth: s.bluetooth, version: s.version };
  }

  async function remap(id, params) {
    const dev = String(params && params.dev || '');
    const map = String(params && params.map || '');
    if (!DEV.test(dev) || map.length > 4096) return fail(id, 'HOST_REFUSED');
    const r = await post('/api/remap?dev=' + dev, map);
    return r.status === 200 ? okay(id, { applied: true }) : fail(id, r.status === 400 ? 'HOST_REFUSED' : 'REQUEST_FAILED');
  }

  async function request(envelope) {
    const id = envelope && envelope.id;
    const method = envelope && envelope.method;
    const params = (envelope && envelope.params) || {};
    if (!envelope || envelope.version !== 1) return fail(id, 'BAD_REQUEST');
    try {
      switch (method) {
        case 'mc.state':
          return okay(id, await state());
        case 'mc.rumble': {
          const slot = Number(params.slot);
          if (!Number.isInteger(slot) || slot < 1 || slot > 4) return fail(id, 'HOST_REFUSED');
          const r = await post('/api/rumble?slot=' + slot);
          return r.status === 200 ? okay(id, { ok: true }) : fail(id, 'HOST_REFUSED');
        }
        case 'mc.remap.apply':
        case 'mc.profile.apply':
          return await remap(id, params);
        default:
          return fail(id, 'UNSUPPORTED');
      }
    } catch (_) {
      return fail(id, 'HOST_UNAVAILABLE');
    }
  }

  function getGlyphs() {
    try { return localStorage.getItem('ob.glyphs') === 'playstation' ? 'playstation' : 'xbox'; } catch (_) { return 'xbox'; }
  }

  if (!window.omnibloomHost) {
    Object.defineProperty(window, 'omnibloomHost', { value: Object.freeze({ request, getGlyphs }), writable: false });
  }
})();
