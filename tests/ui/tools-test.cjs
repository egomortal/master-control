// Master Control 0.4.4 Controller tools (/tools, Astra's R3-049/052/053/054) through the real in-page adapter
// (web/tools/mc-host.js) against tests/ui/stub.py. Usage: node tests/ui/tools-test.cjs [port] [outdir]
const { chromium } = require('playwright');
const port = process.argv[2] || '18767', out = process.argv[3] || 'tests/ui/out-tools';
const base = `http://127.0.0.1:${port}`;
const fs = require('fs'); fs.mkdirSync(out, { recursive: true });
let pass = 0, fail = 0;
const check = (ok, name) => { if (ok) pass++; else fail++; console.log((ok ? 'PASS ' : 'FAIL ') + name); };
const posts = () => fetch(`${base}/__posts`).then(r => r.json());

(async () => {
  const browser = await chromium.launch();
  for (const [w, h] of [[1920, 1080], [3840, 2160]]) {
    const tag = `${w}x${h}`;
    const ctx = await browser.newContext({ viewport: { width: w, height: h } });
    const page = await ctx.newPage();
    const errors = []; page.on('pageerror', e => errors.push(String(e)));
    page.on('console', m => { if (m.type() === 'error') errors.push(m.text()); });
    const csp = []; await page.exposeFunction('__csp', v => csp.push(v));
    await page.addInitScript(() => document.addEventListener('securitypolicyviolation', e => window.__csp(e.violatedDirective + ' ' + e.blockedURI)));

    // index
    await page.goto(base + '/tools'); await page.waitForLoadState('networkidle');
    check(page.url() === base + '/tools/', `${tag} /tools redirects to /tools/`);
    check((await page.$$('.tool')).length === 5, `${tag} index lists four tools and Back`);
    check(await page.evaluate(() => !!document.activeElement && document.activeElement.classList.contains('tool')), `${tag} index starts with a tool focused`);
    const first = await page.evaluate(() => document.activeElement.getAttribute('href'));
    await page.evaluate(() => window.omniPadController('down'));
    check(await page.evaluate(() => document.activeElement.getAttribute('href')) !== first, `${tag} D-pad moves focus`);
    check(await page.evaluate(() => parseFloat(getComputedStyle(document.body).fontSize)) >= 22, `${tag} index text >= 22 px`);
    check(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth), `${tag} index has no sideways overflow`);
    await page.screenshot({ path: `${out}/${tag}-index.png` });

    // info
    await page.goto(base + '/tools/info/'); await page.waitForLoadState('networkidle');
    await page.click('#refresh'); await page.waitForTimeout(300);
    check((await page.textContent('#battery')).trim() === '55%', `${tag} info shows the battery from /api/state`);
    const bodyText = await page.evaluate(() => document.body.innerText + JSON.stringify([...document.querySelectorAll('option')].map(o => o.textContent)));
    check(!/02:00:00:00:00:01/i.test(bodyText), `${tag} info never shows a controller address`);
    check(await page.evaluate(() => document.querySelector('.hint').textContent.includes('A · Select')), `${tag} Xbox labels by default`);
    await page.screenshot({ path: `${out}/${tag}-info.png` });

    // rumble
    const before = (await posts()).length;
    await page.goto(base + '/tools/rumble/'); await page.waitForLoadState('networkidle');
    await page.click('#refresh'); await page.waitForTimeout(200);
    await page.click('#pulse'); await page.waitForTimeout(300);
    const p1 = (await posts()).slice(before);
    check(p1.some(p => p.path === '/api/rumble?slot=1'), `${tag} rumble posts /api/rumble?slot=1 with the menu's header`);
    check(/accepted/.test(await page.textContent('#result')), `${tag} rumble reports the pulse accepted`);

    // layouts
    const before2 = (await posts()).length;
    await page.goto(base + '/tools/layouts/'); await page.waitForLoadState('networkidle');
    await page.click('summary'); await page.fill('#device', '045E_0B13'); await page.click('#review');
    await page.click('#apply'); await page.waitForTimeout(300);
    const p2 = (await posts()).slice(before2);
    check(p2.some(p => p.path === '/api/remap?dev=045E_0B13' && p.body.includes('swap_sticks = 1')), `${tag} layouts apply via /api/remap`);
    check(/Layout applied/.test(await page.textContent('#result')), `${tag} layouts confirm`);

    // profiles
    const before3 = (await posts()).length;
    await page.goto(base + '/tools/profiles/'); await page.waitForLoadState('networkidle');
    await page.click('summary'); await page.fill('#device', '045E_0B13'); await page.selectOption('#target', 'r1');
    await page.click('#save'); await page.click('#apply'); await page.waitForTimeout(300);
    const p3 = (await posts()).slice(before3);
    check(p3.some(p => p.path === '/api/remap?dev=045E_0B13' && p.body.includes('cross = r1')), `${tag} profiles apply via /api/remap`);
    check((await page.textContent('#result')) === 'Layout applied.', `${tag} profiles confirm`);
    await page.screenshot({ path: `${out}/${tag}-profiles.png` });

    check(errors.length === 0, `${tag} no page errors ${errors.join(' | ')}`);
    check(csp.length === 0, `${tag} no CSP violations ${csp.join(' | ')}`);
    await ctx.close();
  }
  await browser.close();
  console.log(`tools UI: ${pass} passed, ${fail} failed`);
  process.exitCode = fail ? 1 : 0;
})().catch(e => { console.error(e); process.exitCode = 1; });
