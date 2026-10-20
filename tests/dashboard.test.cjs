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
  elements.set(id,item);
}
let clock = 10000;
class Clock extends Date { static now() { return clock; } }
const timers = [], intervals = [], requests = [], visibility = [];
const context = {
  document: {getElementById: id => elements.get(id), createElement:()=>node(),addEventListener: (_, fn) => visibility.push(fn)},
  Date: Clock, AbortController,
  setTimeout(fn, ms) { const timer = {fn, ms, active: true}; timers.push(timer); return timer; },
  clearTimeout(timer) { timer.active = false; },
  setInterval(fn) { intervals.push(fn); },
  fetch(url, options) {
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
  assert.equal(requests.length, 1);
  assert.equal(timers.filter(t => t.active && t.ms === 1000).length, 0,
    'no next request while the first one is pending');
  intervals.forEach(fn => fn());
  assert.equal(requests.length, 1);
  await finish(fixture());
  assert.equal(element('firmware').textContent, '0.3.0');
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
  console.log('PASS: serialized polls, discovery text-only rendering/clearing, stale/recovery, age, malformed JSON, timeout, Ethernet error');
})().catch(error => { console.error(error); process.exitCode = 1; });
