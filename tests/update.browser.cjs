// Browser UI contract tests; HTTP is intercepted, not a physical flash/TLS test.
const {chromium}=require('playwright');
const fs=require('node:fs');const assert=require('node:assert/strict');
const html=fs.readFileSync('main/update.html','utf8');
(async()=>{
 const browser=await chromium.launch({headless:true,...(process.env.BROWSER_CHANNEL?{channel:process.env.BROWSER_CHANNEL}:{})});
 let scenarios=0;
 try{
  for(const [name,width,height] of [['phone',375,812],['laptop',1280,900]]){
   for(const host of ['192.168.4.1:8080','192.0.2.10:8080']){
    const page=await browser.newPage({viewport:{width,height}});const errors=[];let mode='valid',uploads=0,cancels=0;
    page.on('pageerror',e=>errors.push(e.message));
    await page.route('**/*',async route=>{
     const req=route.request(),url=new URL(req.url());assert.equal(url.origin,'http://'+host,'no external resource');
     if(url.pathname==='/update')return route.fulfill({contentType:'text/html',body:html});
     assert.equal(req.method(),'POST');assert.equal(req.headers()['x-syndetis-code'],undefined);
     assert.equal(req.headers().origin,'http://'+host);
     if(url.pathname==='/update/cancel'){
      ++cancels;assert.equal(req.headers()['content-type'],'application/json');
      return route.fulfill({status:202,body:'Cancelled'});
     }
     assert.equal(url.pathname,'/update/upload');++uploads;
     assert.equal(req.headers()['content-type'],'application/octet-stream');assert.equal(req.postDataBuffer().length,768);
     if(mode==='disconnect')return route.abort('connectionfailed');
     return route.fulfill({status:mode==='valid'?200:400,body:mode==='valid'?'Verified; rebooting':'Invalid image <img src=x onerror=alert(1)>'});
    });
    const fill=async()=>{
     await page.goto('http://'+host+'/update');assert.equal(await page.locator('#code').count(),0);
     await page.locator('#file').setInputFiles({name:'Syndetis-update.bin',mimeType:'application/octet-stream',buffer:Buffer.alloc(768)});
     assert.ok(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth));
    };
    await fill();await page.locator('#upload').click();await page.waitForFunction(()=>document.getElementById('status').textContent.includes('Verified.'));
    assert.equal(await page.locator('#upload').isDisabled(),true);assert.equal(uploads,1);++scenarios;
    mode='invalid';await fill();await page.locator('#upload').click();await page.waitForFunction(()=>document.getElementById('status').textContent.includes('Invalid image'));
    assert.equal(await page.locator('#status img').count(),0);assert.equal(await page.locator('#upload').isDisabled(),false);++scenarios;
    mode='disconnect';await fill();await page.locator('#upload').click();await page.waitForFunction(()=>document.getElementById('status').textContent.includes('Connection interrupted'));
    assert.equal(await page.locator('#upload').isDisabled(),false);++scenarios;
    await fill();await page.locator('#cancel').click();await page.waitForFunction(()=>document.getElementById('status').textContent.includes('Session cancelled'));
    assert.equal(cancels,1);assert.equal(await page.locator('#upload').isDisabled(),true);++scenarios;
    assert.deepEqual(errors,[]);
    fs.mkdirSync('tmp/browser-screenshots',{recursive:true});await page.screenshot({path:`tmp/browser-screenshots/update-${name}-${host}.png`,fullPage:true});
    await page.close();
   }
  }
  console.log(`PASS: ${scenarios} update UI scenarios: phone/laptop × Wi-Fi/Ethernet × success/rejection/disconnect/cancel, same-origin headers, escaped errors, bounded layout`);
 }finally{await browser.close();}
})().catch(e=>{console.error(e);process.exitCode=1;});
