// Optional integration check: requires Playwright and installed Chrome.
// This intercepts all traffic locally; no tester or external HTTP server is used.
const {chromium} = require('playwright');
const fs = require('node:fs');
const assert = require('node:assert/strict');
const html = fs.readFileSync('main/dashboard.html', 'utf8');
const {status: statusFixture} = require('./fixtures.cjs');

(async () => {
  const browser = await chromium.launch({channel: 'chrome', headless: true});
  try {
    for (const [name, width, height] of [['phone', 375, 812], ['laptop', 1280, 900]]) {
      const page = await browser.newPage({viewport: {width, height}});
      let online = true, down = false, expired = false, external = [], active = 0, maximum = 0;
      const errors = [];
      page.on('pageerror', error => errors.push(error.message));
      await page.route('**/*', async route => {
        const url = new URL(route.request().url());
        if (url.origin !== 'http://syndetis.test') { external.push(url.href); return route.abort(); }
        if (url.pathname === '/') return route.fulfill({contentType: 'text/html', body: html});
        if (url.pathname === '/api/status') {
          active++; maximum = Math.max(maximum, active);
          try {
            if (!online) return await route.abort('connectionfailed');
            await route.fulfill({contentType: 'application/json', body: JSON.stringify(statusFixture(down, expired))});
          } finally { active--; }
          return;
        }
        return route.fulfill({status: 404, body: ''});
      });
      await page.goto('http://syndetis.test/');
      await page.waitForFunction(() => document.getElementById('speed').textContent === '100 Mbps');
      await page.waitForFunction(() => document.querySelectorAll('.neighbor').length===2);
      assert.equal(await page.locator('#neighbors img,#neighbors script').count(),0);
      assert.match(await page.locator('#neighbors').innerText(),/<img src=x/);
      assert.equal(await page.evaluate(()=>window.injected),undefined);
      assert.match(await page.locator('#neighbors').innerText(),/\(empty\)/);
      assert.match(await page.locator('#neighbors').innerText(),/Unavailable/);
      assert.equal(await page.locator('#ip').textContent(), 'Not configured');
      assert.ok(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth), 'no horizontal overflow');
      fs.mkdirSync('tmp/screenshots', {recursive: true});
      await page.screenshot({path: `tmp/screenshots/dashboard-${name}.png`, fullPage: true});
      expired=true;
      await page.waitForFunction(()=>document.querySelectorAll('.neighbor.expired').length===2);
      down = true;
      await page.waitForFunction(() => document.getElementById('link').textContent === 'Down');
      assert.equal(await page.locator('#speed').textContent(), 'Unavailable');
      assert.equal(await page.locator('#duplex').textContent(), 'Unavailable');
      assert.equal(await page.locator('.neighbor').count(),0);
      online = false;
      await page.waitForFunction(() => document.getElementById('connection').textContent.includes('Tester unreachable'));
      assert.ok(await page.locator('#data').evaluate(el => el.classList.contains('stale-data')));
      await page.screenshot({path: `tmp/screenshots/dashboard-${name}-stale.png`, fullPage: true});
      online = true; down = false; expired=false;
      await page.waitForFunction(() => document.getElementById('speed').textContent === '100 Mbps' &&
        !document.getElementById('data').classList.contains('stale-data'));
      assert.equal(maximum, 1);
      assert.deepEqual(external, [], 'no external resources requested');
      assert.deepEqual(errors, []);
      await page.close();
    }
    console.log('PASS: Chrome phone/laptop, long names/XSS as text, null vs empty, expiry/clearing, stale/recovery, no external requests');
  } finally { await browser.close(); }
})().catch(error => {console.error(error); process.exitCode = 1;});
