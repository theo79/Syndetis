// Optional integration check: requires Playwright and installed Chrome.
// This intercepts all traffic locally; no tester or external HTTP server is used.
const {chromium} = require('playwright');
const fs = require('node:fs');
const assert = require('node:assert/strict');
const html = fs.readFileSync('main/dashboard.html', 'utf8');
const {status: statusFixture} = require('./fixtures.cjs');

(async () => {
  const browser = await chromium.launch({headless: true, ...(process.env.BROWSER_CHANNEL ? {channel: process.env.BROWSER_CHANNEL} : {})});
  try {
    for (const [name, width, height] of [['phone', 375, 812], ['laptop', 1280, 900]]) {
      const page = await browser.newPage({viewport: {width, height}});
      let online = true, down = false, expired = false, external = [], active = 0, maximum = 0, dhcpActive=false, manualActive=false, actions=0;
      const errors = [];
      let releaseFirstPoll;
      const firstPoll=new Promise(resolve=>{releaseFirstPoll=resolve;});
      await page.addInitScript(()=>{
        window.pageRequests=[];
        const fetchStatus=window.fetch;
        window.fetch=(url,options)=>{
          window.pageRequests.push((options?.method||'GET')+' '+url);
          return fetchStatus(url,options);
        };
      });
      const saveReport=async()=>{
        const before=await page.evaluate(()=>window.pageRequests.length);
        const filePromise=page.waitForEvent('download');
        await page.locator('#report-download').click();
        const download=await filePromise;
        assert.equal(download.suggestedFilename(),'syndetis-diagnostic-report.json');
        const chunks=[];
        for await(const chunk of await download.createReadStream())chunks.push(chunk);
        const data=JSON.parse(Buffer.concat(chunks).toString('utf8'));
        const exportRequests=await page.evaluate(index=>window.pageRequests.slice(index),before);
        assert.ok(exportRequests.every(request=>request==='GET /api/status'));
        return data;
      };

      page.on('pageerror', error => errors.push(error.message));
      await page.route('**/*', async route => {
        const url = new URL(route.request().url());
        if (url.origin !== 'http://syndetis.test') { external.push(url.href); return route.abort(); }
        if (url.pathname === '/') return route.fulfill({contentType: 'text/html', body: html});
        if(url.pathname==='/api/dhcp/start'||url.pathname==='/api/dhcp/stop'){
          assert.equal(route.request().method(),'POST');assert.equal(route.request().postData(),'{}');
          dhcpActive=url.pathname.endsWith('/start');++actions;
          return route.fulfill({status:202,contentType:'application/json',body:'{"accepted":true}'});
        }
        if (url.pathname === '/api/status') {
          active++; maximum = Math.max(maximum, active);
          try {
            await firstPoll;
            if (!online) return await route.abort('connectionfailed');
            await route.fulfill({contentType: 'application/json', body: JSON.stringify((()=>{const s=statusFixture(down,expired);if(dhcpActive){s.dhcp={...s.dhcp,active:true,state:'Address acquired',ip:'192.0.2.7',mask:'255.255.255.0',gateway:'192.0.2.1',dns:['192.0.2.53',null]};s.ethernet.ip=s.dhcp.ip;}if(manualActive){s.addressing={mode:'MANUAL IP',state:'Manual IP active',prefix:24,gateway:null};s.ethernet.ip='192.168.10.250';s.dhcp.blocked_by_manual=true;}return s;})())});
          } finally { active--; }
          return;
        }
        return route.fulfill({status: 404, body: ''});
      });
      await page.goto('http://syndetis.test/');
      assert.ok(await page.locator('#report-download').isDisabled());
      assert.equal(await page.locator('#report-identifiers').isChecked(),false);
      releaseFirstPoll();
      await page.waitForFunction(() => document.getElementById('speed').textContent === '100 Mbps');
      await page.waitForFunction(() => document.querySelectorAll('#neighbors .neighbor').length===2 &&
        document.querySelectorAll('#profinet-devices .neighbor').length===1);
      assert.equal(await page.locator('#neighbors img,#neighbors script').count(),0);
      assert.match(await page.locator('#neighbors').innerText(),/<img src=x/);
      assert.equal(await page.locator('#profinet-devices img,#profinet-devices script').count(),0);
      assert.match(await page.locator('#profinet-devices h3').innerText(),/<img src=x/);
      assert.match(await page.locator('#profinet-conflicts').innerText(),/duplicate IPv4/);
      assert.equal(await page.evaluate(()=>window.injected),undefined);
      assert.match(await page.locator('#neighbors').innerText(),/\(empty\)/);
      assert.match(await page.locator('#neighbors').innerText(),/Unavailable/);
      assert.equal(await page.locator('#ip').textContent(), 'Not configured');
      assert.ok(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth), 'no horizontal overflow');
      const report=await saveReport();
      assert.equal(report.stale,false);
      assert.equal(report.firmware,'0.5.0');
      assert.equal(Object.hasOwn(report.ethernet,'mac'),false);
      assert.equal(Object.hasOwn(report.switch_info.neighbors[0],'name'),false);
      assert.equal(report.profinet.device_count,1);
      assert.equal(Object.hasOwn(report.profinet,'devices'),false);
      assert.equal(actions,0);
      await page.locator('#dhcp-start').click();
      await page.waitForFunction(()=>document.getElementById('dhcp-ip').textContent==='192.0.2.7');
      assert.equal(await page.locator('#dhcp-start').isDisabled(),true);
      await page.locator('#dhcp-stop').click();
      await page.waitForFunction(()=>document.getElementById('dhcp-ip').textContent==='No current address');
      assert.equal(actions,2);
      manualActive=true;
      await page.waitForFunction(()=>document.getElementById('address-mode').textContent==='MANUAL IP');
      assert.equal(await page.locator('#ip').textContent(),'192.168.10.250');
      assert.equal(await page.locator('#address-prefix').textContent(),'/24');
      assert.equal(await page.locator('#address-gateway').textContent(),'NONE');
      assert.equal(await page.locator('#dhcp-ip').textContent(),'No current address');
      assert.ok(await page.locator('#dhcp-start').isDisabled());
      assert.ok(await page.locator('#dhcp-stop').isDisabled());
      assert.ok(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth));
      manualActive=false;
      await page.waitForFunction(()=>document.getElementById('address-mode').textContent==='NO IP');
      fs.mkdirSync('tmp/browser-screenshots' , {recursive: true});
      await page.screenshot({path: `tmp/browser-screenshots/dashboard-${name}.png`, fullPage: true});
      expired=true;
      await page.waitForFunction(()=>document.querySelectorAll('#neighbors .neighbor.expired').length===2);
      down = true;
      await page.waitForFunction(() => document.getElementById('link').textContent === 'Down');
      assert.equal(await page.locator('#speed').textContent(), 'Unavailable');
      assert.equal(await page.locator('#duplex').textContent(), 'Unavailable');
      assert.equal(await page.locator('#neighbors .neighbor').count(),0);
      assert.match(await page.locator('#profinet-state').innerText(),/Historical results/);
      online = false;
      await page.waitForFunction(() => document.getElementById('connection').textContent.includes('Tester unreachable'));
      assert.ok(await page.locator('#data').evaluate(el => el.classList.contains('stale-data')));
      await page.screenshot({path: `tmp/browser-screenshots/dashboard-${name}-stale.png`, fullPage: true});
      await page.locator('#report-identifiers').check();
      const offline=await saveReport();
      assert.equal(offline.stale,true);
      assert.equal(offline.network_identifiers_included,true);
      assert.equal(offline.ethernet.mac,'02:00:00:00:00:01');
      assert.deepEqual(offline.switch_info.neighbors,[]);
      assert.equal(offline.profinet.valid,false);
      assert.equal(offline.profinet.devices[0].ip,'192.0.2.44');
      assert.equal(Date.parse(offline.exported_at)-Date.parse(offline.snapshot_received_at),offline.snapshot_age_ms);
      assert.equal(actions,2);
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
