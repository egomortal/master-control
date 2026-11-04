// Master Control 0.4.3 menu (Astra's Reactor art, decluttered cards, her 0.4.3 polish) and the /overlay page, driven like the console would: keyboard, Omni's pad hook, the Gamepad
// API's command path. Runs against test/stub.py. Usage: node test/ui-test.cjs [port] [outdir]
const { chromium } = require('playwright');
const port = process.argv[2] || '18766', out = process.argv[3] || 'test/out';
const base = `http://127.0.0.1:${port}`;
const fs = require('fs'); fs.mkdirSync(out, { recursive: true });
let pass = 0, fail = 0;
const check = (ok, name) => { if (ok) pass++; else fail++; console.log((ok ? 'PASS ' : 'FAIL ') + name); };
const mode = m => fetch(`${base}/__mode?m=${m}`);
const posts = () => fetch(`${base}/__posts`).then(r => r.json());

(async () => {
  const browser = await chromium.launch();
  for (const [w, h] of [[1920, 1080], [3840, 2160]]) {
    await mode('rich');
    const page = await browser.newPage({ viewport: { width: w, height: h } });
    const errors = []; page.on('pageerror', e => errors.push(String(e))); page.on('console', m => { if (m.type() === 'error' && !/status of 503/.test(m.text())) errors.push(m.text()); });
    await page.goto(base + '/'); await page.waitForTimeout(1300);
    const tag = `${w}x${h}`;
    check(await page.textContent('#btText') === 'Bluetooth on', `${tag} header shows the Bluetooth status`);
    check((await page.$$('#padGrid .card')).length === 4, `${tag} four cards from /api/state`);
    check(await page.textContent('#coreVal') === '4', `${tag} core shows 4`);
    // nothing overflows the screen
    const over = await page.evaluate(() => document.documentElement.scrollWidth > innerWidth || document.documentElement.scrollHeight > innerHeight + 1);
    check(!over, `${tag} fits the screen (no page scroll)`);
    const fontsOk = await page.evaluate(async () => { await document.fonts.ready; return document.fonts.check('700 20px Rajdhani') && document.fonts.check('400 16px Inter'); });
    check(fontsOk, `${tag} bundled fonts load (no network)`);
    // every card fully visible without scrolling (no clipped second row), lean cards: 3 actions, no id pills
    const clip = await page.evaluate(() => { const b = document.getElementById('pbody').getBoundingClientRect();
      return [].some.call(document.querySelectorAll('#padGrid .card'), c => { const r = c.getBoundingClientRect(); return r.bottom > b.bottom + 1 || r.top < b.top - 1; }); });
    check(!clip, `${tag} all four cards visible, nothing cut off`);
    const lean = await page.evaluate(() => [].every.call(document.querySelectorAll('#padGrid .card'), c => c.querySelectorAll('.acts .btn').length === 3 && !/[0-9a-f]{4}:[0-9a-f]{4}/.test(c.textContent)));
    check(lean, `${tag} cards show Remap, Press PS, More and no vendor ids`);
    check(await page.evaluate(() => /Pressed:/.test(document.getElementById('padGrid').textContent)), `${tag} pressed inputs are labelled "Pressed"`);
    check(await page.evaluate(() => !/Every controller Master Control is handling|How Master Control talks to you/.test(document.body.innerText)), `${tag} redundant header blurbs are gone`);
    if (w === 1920) {
      const small = await page.evaluate(() => { const bad = []; const walk = document.createTreeWalker(document.body, NodeFilter.SHOW_TEXT);
        while (walk.nextNode()) { const n = walk.currentNode, e = n.parentElement; if (!n.textContent.trim() || !e || !e.getClientRects().length) continue;
          const cs = getComputedStyle(e); if (cs.visibility === 'hidden' || cs.display === 'none') continue;
          if (parseFloat(cs.fontSize) < 22) bad.push(n.textContent.trim().slice(0, 20) + '@' + cs.fontSize); } return bad; });
      check(small.length === 0, `${tag} no visible text under 22 px ${small.slice(0, 5).join(', ')}`);
    }
    check(await page.evaluate(() => !/Ready/.test(document.getElementById('padGrid').textContent) && /Waiting for a free player/.test(document.getElementById('padGrid').textContent)), `${tag} only non-ready states are named`);
    const art = await page.evaluate(() => { const i = document.getElementById('deviceArt'); return i.complete && i.naturalWidth === 1024 && /^data:image\/webp/.test(i.src); });
    check(art, `${tag} Reactor art is the 1024 px WebP`);
    await page.screenshot({ path: `${out}/${tag}-pads.png` });
    // R1 through every section, by keyboard
    const styleAng = () => page.evaluate(() => parseFloat(/rotate\(([-0-9.]+)deg\)/.exec(document.getElementById('deviceArt').style.transform)[1]));
    let prevAng = await styleAng();
    for (const sec of ['pair', 'remap', 'overlay', 'setup', 'pads']) {
      await page.keyboard.press('PageDown'); await page.waitForTimeout(400);
      const shown = await page.$eval(`.sec[data-sec="${sec}"]`, e => !e.hidden);
      const fin = await page.evaluate(() => document.activeElement && document.activeElement.dataset.sec);
      check(shown && fin === sec && (await page.textContent('#ptitle')).length > 3, `${tag} R1 turns to ${sec}, focus on its fin, title shown`);
      const nowAng = await styleAng(); check(nowAng - prevAng === 72, `${tag} ${sec}: one 72° step forward`); prevAng = nowAng;
      const ang = await page.evaluate(() => { const m = new DOMMatrix(getComputedStyle(document.getElementById('deviceArt')).transform); return Math.round((Math.atan2(m.b, m.a) * 180 / Math.PI + 360) % 360); });
      const want = ((['pads','pair','remap','overlay','setup'].indexOf(sec) - 1) * 72 + 360) % 360;
      check(Math.abs(ang - want) <= 1 || Math.abs(ang - want) >= 359, `${tag} ${sec}: lit fin turned to its section (${ang}°)`);
      await page.screenshot({ path: `${out}/${tag}-${sec}.png` });
    }
    // L1 back one
    await page.keyboard.press('PageUp'); await page.waitForTimeout(100);
    check(await page.$eval('.sec[data-sec="setup"]', e => !e.hidden), `${tag} L1 turns back to setup`);
    // Into a panel and around, with Omni's pad hook
    await page.evaluate(() => window.omniPadController('r1')); // -> pads
    await page.evaluate(() => window.omniPadController('cross')); // enter panel
    let a = await page.evaluate(() => document.activeElement.textContent);
    check(/Press PS|Remap|More/.test(a), `${tag} cross enters the panel (${a.trim()})`);
    for (let i = 0; i < 6; i++) await page.evaluate(() => window.omniPadController('down'));
    const vis = await page.evaluate(() => { const r = document.activeElement.getBoundingClientRect(); return r.top >= 0 && r.bottom <= innerHeight; });
    check(vis, `${tag} focus stays on screen after six downs`);
    check(await page.evaluate(() => window.omniPadController('circle')) === true, `${tag} circle returns to the dial`);
    check(await page.evaluate(() => document.activeElement.classList.contains('fin')), `${tag} focus is on a fin after circle`);
    check(await page.evaluate(() => window.omniPadController('circle')) === false, `${tag} circle on the dial is left to Omni`);
    // Triangle from a card selects that model in remap
    await page.evaluate(() => window.omniPadController('cross'));
    await page.keyboard.press('r'); await page.waitForTimeout(400);
    check(await page.$eval('.sec[data-sec="remap"]', e => !e.hidden), `${tag} triangle opens remap`);
    // 0.4.3 (Astra): only the changed buttons at first, Show all buttons for the full map, no USB ids
    check((await page.$$('#rmRows .rm')).length === 2 && (await page.$$('#rmRows .rm.chg')).length === 2 && await page.$eval('#rmNoChanges', e => e.hidden), `${tag} remap starts with the 2 changed buttons only`);
    check(await page.$eval('.sec[data-sec="remap"]', e => /Custom map/.test(e.textContent) && !/[0-9a-f]{4}:[0-9a-f]{4}/i.test(e.textContent)), `${tag} model selector says Custom/Default map, no USB ids`);
    if (w === 1920) await page.screenshot({ path: `${out}/${tag}-remap-changed.png` });
    await page.click('#rmAll'); await page.waitForTimeout(100);
    check(await page.textContent('#rmAll') === 'Show changed buttons', `${tag} Show all buttons toggles`);
    check((await page.$$('#rmRows .rm')).length === 24 && (await page.$$('#rmRows .rm:not([hidden])')).length === 18, `${tag} full map: 18 rows, 6 unused paddles folded`);
    await page.click('#rmExtra'); await page.waitForTimeout(100);
    check((await page.$$('#rmRows .rm:not([hidden])')).length === 24, `${tag} extra buttons unfold to 24 rows`);
    await page.click('#rmExtra');
    check(await page.$eval('#rmRows [data-rm="cross"] .t', e => e.textContent) === 'B', `${tag} saved map shows A→B (Xbox labels)`);
    await page.click('#glyphPS'); await page.waitForTimeout(50);
    check(await page.$eval('#rmRows [data-rm="cross"] .t', e => e.textContent) === '○' && await page.textContent('#hintSelect') === '✕', `${tag} PlayStation labels switch display only`);
    await page.click('#glyphXbox'); await page.waitForTimeout(50);
    // change A to triangle via the picker, then save
    await page.focus('#rmRows [data-rm="cross"]'); await page.keyboard.press('Enter'); await page.waitForTimeout(100);
    check(!(await page.$eval('#picker', e => e.hidden)), `${tag} picker opens`);
    await page.evaluate(() => [].find.call(document.querySelectorAll('#pickGrid button'), b => b.textContent === 'Y').click()); await page.waitForTimeout(100);
    check(await page.$eval('#rmRows [data-rm="cross"] .t', e => e.textContent) === 'Y' && !(await page.$eval('#rmUnsaved', e => e.hidden)), `${tag} picked target shows and is marked unsaved`);
    await page.click('#rmSave'); await page.waitForTimeout(300);
    const p1 = await posts(); const save = p1.filter(p => p.path === '/api/remap?dev=045E_0B13').pop();
    check(save && /cross = triangle/.test(save.body) && /invert_ry = 1/.test(save.body), `${tag} save posts the map text with options`);
    // More: secondary actions behind one button
    await page.click('.fin[data-sec="pads"]'); await page.waitForTimeout(150);
    const mores = await page.$$('#padGrid .card .acts .btn.quiet');
    await mores[0].click(); await page.waitForTimeout(100);
    const sheet = await page.evaluate(() => ({ open: !document.getElementById('picker').hidden, text: document.getElementById('pickGrid').textContent, focus: document.activeElement.textContent }));
    check(sheet.open && /045e:0b13/.test(sheet.text) && /Identify/.test(sheet.text) && /Forget/.test(sheet.text), `${tag} More shows details, Identify and Forget`);
    if (w === 1920) await page.screenshot({ path: `${out}/${tag}-more.png` });
    await page.evaluate(() => [].find.call(document.querySelectorAll('#pickGrid button'), b => /Identify/.test(b.textContent)).click()); await page.waitForTimeout(300);
    check((await posts()).some(p => p.path === '/api/rumble?slot=1') && await page.$eval('#picker', e => e.hidden), `${tag} Identify rumbles and closes`);
    await mores[0].click(); await page.waitForTimeout(100);
    const fgClick = () => page.evaluate(() => [].find.call(document.querySelectorAll('#pickGrid button'), b => /forget/i.test(b.textContent)).click());
    const nForget = async () => (await posts()).filter(p => /^\/api\/forget/.test(p.path)).length, f0 = await nForget();
    await fgClick(); await page.waitForTimeout(100);
    check(await nForget() === f0, `${tag} Forget asks first`);
    await fgClick(); await page.waitForTimeout(300);
    check(await nForget() === f0 + 1 && (await posts()).some(p => p.path === '/api/forget?addr=02:00:00:00:00:01'), `${tag} Forget on second press`);
    await mores[1].click(); await page.waitForTimeout(100);
    check(!/Identify|Forget/.test(await page.textContent('#pickGrid')) && /USB/.test(await page.textContent('#pickGrid')), `${tag} USB card's More has details only`);
    await page.keyboard.press('Escape'); await page.waitForTimeout(50);
    check(await page.$eval('#picker', e => e.hidden), `${tag} escape closes More`);
    // pair via square
    await page.keyboard.press('p'); await page.waitForTimeout(100);
    check(await page.evaluate(() => document.activeElement.id) === 'pairBtn', `${tag} square focuses Search`);
    await page.keyboard.press('Enter'); await page.waitForTimeout(1300);
    check((await posts()).some(p => p.path === '/api/pair'), `${tag} search posts /api/pair`);
    check(/Searching/.test(await page.textContent('#pairBtn')), `${tag} pairing countdown shows`);
    // language picker in setup
    await page.click('.fin[data-sec="setup"]'); await page.waitForTimeout(100);
    await page.click('#langBtn'); await page.click('#pickGrid button:has-text("Español")'); await page.waitForTimeout(1300);
    check((await posts()).some(p => p.path === '/api/lang?code=es') && (await page.textContent('#langBtn')) === 'Español', `${tag} language set on the console`);
    await page.evaluate(() => fetch('/api/lang?code=en', { method: 'POST', headers: { 'X-PadBridge': '1' } }));
    // log view
    await page.click('#logBtn'); await page.waitForTimeout(300);
    check(/listening/.test(await page.textContent('#logText')), `${tag} log view loads /api/log`);
    await page.keyboard.press('Escape');
    check(await page.$eval('#logView', e => e.hidden), `${tag} escape closes the log`);
    // classic menu still served
    const classic = await (await fetch(base + '/classic')).text();
    check(/EeveeLution|PadBridge/.test(classic), `${tag} classic menu reachable at /classic`);
    // stop is armed
    await page.click('#stopBtn'); check(!(await posts()).some(p => p.path === '/api/stop'), `${tag} stop asks first`);
    // states: recorded console, bluetooth failed, API down
    await mode('recorded'); await page.click('.fin[data-sec="pads"]'); await page.waitForTimeout(1300);
    check(!(await page.$eval('#padsEmpty', e => e.hidden)) && (await page.textContent('#coreVal')) === '0', `${tag} recorded console state: empty state`);
    await page.screenshot({ path: `${out}/${tag}-recorded-empty.png` });
    await mode('btfail'); await page.waitForTimeout(1300);
    check(/Bluetooth chip/.test(await page.textContent('#banner')), `${tag} bluetooth failure banner`);
    await page.click('.fin[data-sec="pair"]'); await page.waitForTimeout(150);
    check(!(await page.$eval('#retryBtn', e => e.hidden)) && await page.$eval('#pairBtn', e => e.disabled), `${tag} retry shown, search disabled`);
    await page.screenshot({ path: `${out}/${tag}-btfail.png` });
    await mode('down'); await page.waitForTimeout(1300);
    check(await page.textContent('#coreVal') === '!' && /isn't answering/.test(await page.textContent('#banner')) && await page.textContent('#btText') === 'Offline', `${tag} API down state`);
    await page.screenshot({ path: `${out}/${tag}-down.png` });
    check(errors.length === 0, `${tag} no page errors ${errors.join(' | ')}`);
    await page.close();
    // the quick overlay (0.4.3): what Omni opens for the overlay.show intent
    await mode('rich');
    const ov = await browser.newPage({ viewport: { width: w, height: h } });
    const oerr = []; ov.on('pageerror', e => oerr.push(String(e)));
    await ov.goto(base + '/overlay'); await ov.waitForTimeout(800);
    check(await ov.title() === 'Master Control · Quick overlay', `${tag} overlay served`);
    check((await ov.$$('#pads li')).length === 4 && /4 controllers/.test(await ov.textContent('#status')), `${tag} overlay lists the four controllers`);
    check(await ov.evaluate(() => document.activeElement.id) === 'pair', `${tag} overlay focuses Pair first`);
    check(await ov.evaluate(() => window.omniPadController('right')) === true && await ov.evaluate(() => document.activeElement.id) === 'full', `${tag} overlay D-pad moves focus`);
    await ov.evaluate(() => window.omniPadController('left'));
    const np0 = (await posts()).filter(p => p.path === '/api/pair').length;
    await ov.evaluate(() => window.omniPadController('cross')); await ov.waitForTimeout(400);
    check((await posts()).filter(p => p.path === '/api/pair').length === np0 + 1, `${tag} overlay Cross on Pair posts /api/pair (with the header)`);
    check(await ov.evaluate(() => window.omniPadController('square')) === false, `${tag} overlay leaves unknown buttons to Omni`);
    const ofit = await ov.evaluate(() => document.documentElement.scrollWidth <= innerWidth);
    check(ofit, `${tag} overlay has no horizontal scroll`);
    if (w === 1920) {
      const osmall = await ov.evaluate(() => [].filter.call(document.querySelectorAll('body *'), e => e.children.length === 0 && e.textContent.trim() && e.getClientRects().length && parseFloat(getComputedStyle(e).fontSize) < 22).map(e => e.textContent.trim().slice(0, 20)));
      check(osmall.length === 0, `${tag} overlay text at least 22 px ${osmall.join(', ')}`);
    }
    await ov.screenshot({ path: `${out}/${tag}-overlay.png` });
    await mode('down'); await ov.waitForTimeout(1800);
    check(/Not answering/.test(await ov.textContent('#status')) && await ov.$eval('#pair', e => e.disabled), `${tag} overlay shows the service down and disables Pair`);
    check(oerr.length === 0, `${tag} overlay has no page errors ${oerr.join(' | ')}`);
    await ov.close();
  }
  // health, as Bloom Core asks for it
  await mode('rich');
  const hres = await (await fetch(base + '/api/health')).json();
  check(hres.ok === true && hres.version === '0.4.3', 'GET /api/health answers ok and the version');
  // POSTs without the header are refused by the payload; the page always sends it
  const all = await posts();
  check(all.length > 0, 'stub recorded posts (all carried X-PadBridge, others are refused with 403)');
  await browser.close();
  console.log(`${pass} passed, ${fail} failed`);
  process.exit(fail ? 1 : 0);
})();
