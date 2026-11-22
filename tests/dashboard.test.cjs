// Run with: node tests/dashboard.test.cjs
// Execute the actual embedded script with a small DOM/fetch/timer harness.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const {status: statusFixture} = require('./fixtures.cjs');

function node(){
  const classes=new Set();
  const result={textContent:'',hidden:false,children:[],append(...items){this.children.push(...items);},
    replaceChildren(){this.children=[];},
    classList:{add:x=>classes.add(x),remove:x=>classes.delete(x),contains:x=>classes.has(x)}};
  Object.defineProperty(result,'innerHTML',{set(){throw new Error('Network strings must never use innerHTML');}});
  return result;
}

const html = fs.readFileSync('main/dashboard.html', 'utf8');
const script = html.match(/<script>([\s\S]*?)<\/script>/)[1];
const elements = new Map();
for (const [, id, classes] of html.matchAll(/id="([^"]+)"(?: class="([^"]*)")?/g)) {
  const values = new Set((classes || '').split(' '));
  const item=node();
  for(const name of values)item.classList.add(name);
  if(id==='report-download')item.disabled=/<button id="report-download" disabled>/.test(html);
  if(id==='report-identifiers')item.checked=/<input[^>]+id="report-identifiers"[^>]+\bchecked\b/.test(html);
  elements.set(id,item);
}
let clock = 10000;
class Clock extends Date { static now() { return clock; } }
const actions = [];
const downloads = [], revoked = [], blobs = new Map();
const timers = [], intervals = [], requests = [], visibility = [];
const context = {
  document: {
    getElementById: id => elements.get(id), body:node(),
    createElement(tag) {
      if(tag!=='a')return node();
      return {click(){downloads.push({blob:blobs.get(this.href),download:this.download});},remove(){}};
    },
    addEventListener: (_, fn) => visibility.push(fn),
  },
  Blob,
  URL:{
    createObjectURL(blob){const href='blob:'+blobs.size;blobs.set(href,blob);return href;},
    revokeObjectURL(href){assert.ok(blobs.delete(href));revoked.push(href);},
  },
  Date: Clock, AbortController,
  setTimeout(fn, ms) { const timer = {fn, ms, active: true}; timers.push(timer); return timer; },
  clearTimeout(timer) { timer.active = false; },
  setInterval(fn) { intervals.push(fn); },
  fetch(url, options) {
    if(options.method==='POST'){
      assert.ok(['/api/dhcp/start','/api/dhcp/stop'].includes(url));
      assert.equal(options.headers['Content-Type'],'application/json');assert.equal(options.body,'{}');
      return new Promise((resolve,reject)=>{actions.push({url,options,resolve,reject});options.signal.addEventListener('abort',()=>reject(new Error('timeout')));});
    }
    assert.equal(url, '/api/status');
    assert.equal(options.cache, 'no-store');
    return new Promise((resolve, reject) => {
      requests.push({resolve, reject});
      options.signal.addEventListener('abort', () => reject(new Error('timeout')));
    });
  },
};
const tick = () => new Promise(resolve => setImmediate(resolve));
const element = id => elements.get(id);
const fixture = (link = 'Up') => statusFixture(link==='Down');
const finish = async value => { requests.at(-1).resolve({ok: true, json: async () => value}); await tick(); };
const next = () => {
  const timer = timers.find(t => t.active && t.ms === 1000);
  assert.ok(timer, 'next poll should be scheduled after completion');
  timer.active = false;
  timer.fn();
};

(async () => {
  vm.runInNewContext(script, context);
  assert.equal(element('report-download').disabled,true);
  element('report-download').onclick();
  assert.equal(downloads.length,0);
  assert.equal(requests.length, 1);
  assert.equal(timers.filter(t => t.active && t.ms === 1000).length, 0,
    'no next request while the first one is pending');
  intervals.forEach(fn => fn());
  assert.equal(requests.length, 1);
  await finish(fixture());
  assert.equal(element('firmware').textContent, '0.5.0');
  assert.equal(element('neighbors').children.length,2);
  assert.equal(element('speed').textContent, '100 Mbps');
  assert.equal(element('duplex').textContent, 'Full');
  assert.equal(element('ip').textContent, 'Not configured');
  assert.ok(!element('data').classList.contains('stale-data'));
  next();
  requests.at(-1).reject(new Error('disconnected'));
  await tick();
  assert.match(element('connection').textContent, /Tester unreachable/);
  assert.ok(element('data').classList.contains('stale-data'));
  assert.equal(element('speed').textContent, '100 Mbps', 'retain last values, visibly stale');
  next();
  await finish(fixture('Down'));
  assert.equal(element('link').textContent, 'Down');
  assert.equal(element('speed').textContent, 'Unavailable');
  assert.equal(element('duplex').textContent, 'Unavailable');
  assert.equal(element('neighbors').children.length,0,'clear identity on link down');
  assert.ok(!element('data').classList.contains('stale-data'), 'recover automatically');
  clock += 5000;
  visibility.forEach(fn => fn());
  assert.ok(element('data').classList.contains('stale-data'), 'old data stale after tab suspension');
  next();
  await finish({bad: 'payload'});
  assert.match(element('connection').textContent, /Tester unreachable/);
  next();
  const abort = timers.find(t => t.active && t.ms === 2500);
  assert.ok(abort);
  abort.active = false;
  abort.fn();
  await tick();
  assert.match(element('connection').textContent, /Tester unreachable/);
  next();
  const failed = fixture();
  failed.ethernet.initialization = 'Error';
  failed.ethernet.link = 'Unavailable';
  failed.ethernet.speed_mbps = null;
  failed.ethernet.duplex = 'Unavailable';
  failed.ethernet.error_step = 'Driver install';
  failed.ethernet.error_code = 263;
  await finish(failed);
  assert.equal(element('error').hidden, false);
  assert.match(element('error').textContent, /Driver install.*263/);
  assert.ok(!element('data').classList.contains('stale-data'));
  next();await finish(fixture());
  element('dhcp-start').onclick();assert.equal(actions.length,1);assert.equal(element('dhcp-start').disabled,true);
  element('dhcp-start').onclick();assert.equal(actions.length,1,'reject concurrent actions');
  actions.at(-1).resolve({status:202});await tick();assert.match(element('dhcp-action').textContent,/accepted/);
  next();const requesting=fixture();requesting.dhcp.requesting=true;requesting.dhcp.state='Requesting address';requesting.dhcp.duration_ms=1200;await finish(requesting);
  assert.equal(element('dhcp-start').disabled,true);assert.equal(element('dhcp-time').textContent,'1.2 s');
  element('dhcp-stop').onclick();assert.equal(actions.at(-1).url,'/api/dhcp/stop');actions.at(-1).resolve({status:409});await tick();assert.match(element('dhcp-action').textContent,/operation active/);
  next();const active=fixture();active.dhcp={...active.dhcp,active:true,state:'Address acquired',ip:'192.0.2.7',mask:'255.255.255.0',gateway:'192.0.2.1',dns:['192.0.2.53',null]};active.ethernet.ip=active.dhcp.ip;await finish(active);
  assert.equal(element('dhcp-ip').textContent,'192.0.2.7');assert.equal(element('dhcp-start').disabled,true);
  next();await finish(fixture());assert.equal(element('dhcp-ip').textContent,'No current address');
  next();const cleanup=fixture();cleanup.dhcp={...cleanup.dhcp,state:'No lease within 30 s',cleanup_error:true,cleanup_code:20481,cleanup_step:'restore DNS'};await finish(cleanup);
  assert.match(element('dhcp-state').textContent,/No lease within 30 s.*Cleanup: restore DNS.*20481/);
  assert.equal(element('dhcp-start').disabled,true);
  next();const manual=fixture();manual.addressing={mode:'MANUAL IP',state:'Manual IP active',prefix:24,gateway:null};manual.ethernet.ip='192.168.10.250';manual.dhcp.blocked_by_manual=true;await finish(manual);
  assert.equal(element('address-mode').textContent,'MANUAL IP');assert.equal(element('ip').textContent,'192.168.10.250');assert.equal(element('address-prefix').textContent,'/24');
  assert.equal(element('dhcp-ip').textContent,'No current address');assert.equal(element('address-gateway').textContent,'NONE');assert.equal(element('dhcp-start').disabled,true);assert.equal(element('dhcp-stop').disabled,true);
  next();const conflict=fixture();conflict.addressing={mode:'NO IP',state:'ADDRESS IN USE',prefix:0,gateway:null};await finish(conflict);assert.equal(element('ip').textContent,'Not configured');assert.equal(element('address-state').textContent,'ADDRESS IN USE');
  next();requests.at(-1).reject(new Error('offline'));await tick();assert.equal(element('dhcp-start').disabled,true);assert.equal(element('dhcp-stop').disabled,true);

  // A successful status commits one report snapshot, independent of the DOM.
  next();
  const reportStatus=fixture();
  reportStatus.addressing={mode:'NO IP',state:'IPv4 off / passive',prefix:0,gateway:null};
  reportStatus.switch_info.neighbors[0].port_description='<img src=x> "quotes" \\ slash\n\u0001 caf\u00e9';
  reportStatus.switch_info.neighbors[0].expired=true;
  reportStatus.switch_info.neighbors[0].age_seconds=121;
  delete reportStatus.switch_info.neighbors[1].port_id;
  await finish(reportStatus);
  const receipt=clock;
  const getReport=async()=>{
    const requestsBefore=requests.length, actionsBefore=actions.length;
    element('report-download').onclick();
    assert.equal(requests.length,requestsBefore);
    assert.equal(actions.length,actionsBefore);
    const {blob,download}=downloads.at(-1);
    assert.equal(download,'syndetis-diagnostic-report.json');
    assert.equal(blob.type,'application/json');
    return JSON.parse(await blob.text());
  };
  assert.equal(element('report-download').disabled,false);
  assert.equal(element('report-identifiers').checked,false);
  clock+=200;
  const redacted=await getReport();
  assert.equal(redacted.schema_version,1);
  assert.equal(redacted.firmware,reportStatus.firmware);
  assert.equal(redacted.uptime_seconds,reportStatus.uptime_seconds);
  assert.equal(redacted.snapshot_received_at,new Date(receipt).toISOString());
  assert.equal(redacted.exported_at,new Date(clock).toISOString());
  assert.equal(redacted.snapshot_age_ms,200);
  assert.equal(redacted.stale,false);
  assert.equal(redacted.network_identifiers_included,false);
  assert.equal(redacted.ping_statistics,'unavailable');
  assert.equal(redacted.device_wall_clock,'unavailable');
  assert.equal(redacted.dhcp.error_step,null);
  const [lldp,cdp]=redacted.switch_info.neighbors;
  assert.deepEqual(lldp,{slot:1,protocol:'LLDP',ttl_seconds:120,age_seconds:121,expired:true,sanitized:false});
  assert.equal(cdp.protocol,'CDP');
  for(const [object, keys] of [
    [redacted.ethernet,['mac','ip']], [redacted.addressing,['prefix','gateway']],
    [redacted.dhcp,['ip','mask','gateway','dns']],
    [lldp,['source_mac','name','chassis','port_id','port_description','management_ip','management_source','vlan_kind','vlan']]
  ])for(const key of keys)assert.ok(!Object.hasOwn(object,key),key);

  element('report-identifiers').checked=true;
  next();
  const partial=fixture();
  partial.firmware='should not be exported';
  delete partial.dhcp.dns; // Passes initial checks and fails while rendering DHCP.
  await finish(partial);
  const optedIn=await getReport();
  assert.equal(optedIn.stale,true);
  assert.equal(optedIn.network_identifiers_included,true);
  assert.equal(optedIn.firmware,reportStatus.firmware);
  assert.equal(optedIn.snapshot_received_at,redacted.snapshot_received_at);
  assert.deepEqual(optedIn.ethernet,reportStatus.ethernet);
  assert.deepEqual(optedIn.addressing,reportStatus.addressing);
  assert.deepEqual(optedIn.dhcp.dns,[null,null]);
  assert.deepEqual(optedIn.switch_info.neighbors,reportStatus.switch_info.neighbors);
  assert.equal(optedIn.switch_info.neighbors[1].chassis,null);
  assert.ok(!Object.hasOwn(optedIn.switch_info.neighbors[1],'port_id'));

  next();
  requests.at(-1).reject(new Error('offline'));
  await tick();
  clock+=4800;
  const offline=await getReport();
  assert.equal(offline.snapshot_age_ms,5000);
  assert.equal(offline.stale,true);
  assert.deepEqual(offline.switch_info,optedIn.switch_info);
  next();
  await finish(fixture('Down'));
  assert.equal((await getReport()).stale,false);
  clock+=4001; // No timer or visibility event is needed to detect stale data on click.
  const aged=await getReport();
  assert.equal(aged.stale,true);
  assert.equal(aged.ethernet.speed_mbps,null);
  assert.equal(aged.ethernet.duplex,'Unavailable');
  assert.deepEqual(aged.switch_info.neighbors,[]);
  assert.ok(!Object.hasOwn(aged,'addressing'));
  timers.filter(t=>t.active&&t.ms===1500).forEach(t=>t.fn());
  assert.equal(revoked.length,downloads.length);

  console.log('PASS: report download, redaction, escaping, stale snapshots, timing and object URL cleanup');
  console.log('PASS: serialized polls, discovery text-only rendering/clearing, stale/recovery, age, malformed JSON, timeout, Ethernet error');
})().catch(error => { console.error(error); process.exitCode = 1; });
