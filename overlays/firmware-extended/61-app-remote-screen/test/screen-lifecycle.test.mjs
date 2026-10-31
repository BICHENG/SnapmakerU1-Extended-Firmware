import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {webcrypto} from 'node:crypto';
import vm from 'node:vm';
import test from 'node:test';

const source = readFileSync(new URL('../root/usr/local/share/fb-http/html/screen.js', import.meta.url), 'utf8');
const deferred = () => {
    let resolve, reject;
    const promise = new Promise((yes, no) => { resolve = yes; reject = no; });
    return {promise, resolve, reject};
};
const settle = async () => { for (let i = 0; i < 40; ++i) await Promise.resolve(); };

function browser({storage = new Map(), navigation = 'navigate', frameNavigation = navigation,
                  auth = async () => true, request, answer} = {}) {
    const calls = [], peers = [], timers = new Map(), events = new Map();
    const elements = new Map(['screen', 'status', 'reconnect', 'fullscreen'].map(id => [id, {
        addEventListener() {}, getBoundingClientRect: () => ({left:0, top:0, width:480, height:320})
    }]));
    let nextId = 0, nextTimer = 0;
    class Peer {
        constructor() { this.connectionState = 'new'; peers.push(this); }
        async setRemoteDescription() {}
        async createAnswer() { return answer ? answer(this) : {type:'answer', sdp:'answer'}; }
        async setLocalDescription(value) { this.localDescription = value; }
        close() { this.closed = true; this.connectionState = 'closed'; this.onconnectionstatechange?.(); }
        change(state) { this.connectionState = state; this.onconnectionstatechange?.(); }
    }
    const context = vm.createContext({
        document:{getElementById:id => elements.get(id)},
        window:{top:{performance:{getEntriesByType:() => [{type:navigation}]}},
            performance:{getEntriesByType:() => [{type:frameNavigation}]},
            addEventListener:(type, fn) => events.set(type, fn)},
        sessionStorage:{getItem:key => storage.get(key) ?? null, setItem:(key, value) => storage.set(key, value)},
        crypto:webcrypto, Uint8Array, RTCPeerConnection:Peer, MediaStream:class {},
        initAuth:auth, getAuthHeaders:() => ({Authorization:'test-token'}),
        setTimeout:(fn, delay) => { const id = ++nextTimer; timers.set(id, {fn, delay}); return id; },
        clearTimeout:id => timers.delete(id),
        fetch:async (url, options) => {
            const message = JSON.parse(options.body); calls.push({message, options});
            let result = {type:'ok'};
            if (message.type === 'request') {
                result = request ? await request(message) : {id:String(++nextId), sdp:'offer'};
            }
            return {ok:true, json:async () => result};
        }
    });
    vm.runInContext(source, context);
    return {calls, peers, timers, events, storage, elements,
        connect:() => vm.runInContext('connect()', context),
        async retry() {
            const [id, {fn}] = timers.entries().next().value;
            timers.delete(id); fn(); await settle();
        }};
}

test('pagehide explicitly releases the server session with authenticated keepalive fetch', async () => {
    const b = browser(); await settle();
    b.events.get('pagehide')(); await settle();
    const close = b.calls.find(c => c.message.type === 'close');
    assert.equal(close.message.id, '1');
    assert.equal(close.options.keepalive, true);
    assert.equal(close.options.headers.Authorization, 'test-token');
    assert.equal(b.peers[0].closed, true);
    assert.equal(b.timers.size, 0);
});

test('Fluidd iframe reuses its tab identity when only the top navigation reports reload', async () => {
    const first = browser(); await settle();
    const original = first.calls.find(c => c.message.type === 'request').message;
    const refreshed = browser({storage:first.storage, navigation:'reload', frameNavigation:'navigate'});
    await settle();
    const next = refreshed.calls.find(c => c.message.type === 'request').message;
    assert.equal(next.screen_owner, original.screen_owner);
    assert.equal(next.screen_generation, original.screen_generation + 1);
    const frameReload = browser({storage:first.storage, navigation:'navigate', frameNavigation:'reload'});
    await settle();
    const nextFrame = frameReload.calls.find(c => c.message.type === 'request').message;
    assert.equal(nextFrame.screen_owner, original.screen_owner);
    assert.equal(nextFrame.screen_generation, original.screen_generation + 2);
    const separate = browser({storage:new Map(first.storage), navigation:'navigate'}); await settle();
    assert.notEqual(separate.calls[0].message.screen_owner, original.screen_owner);
});

test('stale authentication does not allocate another server session', async () => {
    const firstAuth = deferred(); let count = 0;
    const b = browser({auth:() => ++count === 1 ? firstAuth.promise : Promise.resolve(true)});
    await b.connect(); firstAuth.resolve(true); await settle();
    assert.equal(b.calls.filter(c => c.message.type === 'request').length, 1);
});

test('a stale offer is closed by its own id and cannot close the replacement', async () => {
    const firstOffer = deferred(); let count = 0;
    const b = browser({request:() => ++count === 1 ? firstOffer.promise : {id:'new', sdp:'offer'}});
    await settle(); await b.connect();
    firstOffer.resolve({id:'old', sdp:'offer'}); await settle();
    assert.ok(b.calls.some(c => c.message.type === 'close' && c.message.id === 'old'));
    assert.ok(!b.calls.some(c => c.message.type === 'close' && c.message.id === 'new'));
    assert.equal(b.peers.length, 1);
    assert.ok(!b.peers[0].closed);
});

test('superseding an asynchronous answer closes only the obsolete peer and sends no obsolete answer', async () => {
    const firstAnswer = deferred(); let count = 0;
    const b = browser({answer:() => ++count === 1 ? firstAnswer.promise : {type:'answer', sdp:'answer'}});
    await settle(); await b.connect();
    firstAnswer.resolve({type:'answer', sdp:'old-answer'}); await settle();
    assert.equal(b.peers[0].closed, true);
    assert.ok(!b.peers[1].closed);
    assert.ok(!b.calls.some(c => c.message.type === 'answer' && c.message.id === '1'));
    assert.ok(b.calls.some(c => c.message.type === 'close' && c.message.id === '1'));
});

test('failed connections release their slot and retry with a finite backoff', async () => {
    const b = browser(); await settle(); b.peers[0].change('connected');
    b.peers[0].change('disconnected'); await settle();
    assert.ok(b.calls.some(c => c.message.type === 'close' && c.message.id === '1'));
    assert.equal(b.timers.size, 1); await b.retry();
    assert.equal(b.calls.filter(c => c.message.type === 'request').length, 2);
    const failing = browser({request:() => ({error:'max clients reached'})}); await settle();
    for (let i = 0; i < 5; ++i) await failing.retry();
    assert.equal(failing.timers.size, 0);
    assert.equal(failing.calls.filter(c => c.message.type === 'request').length, 6);
});

test('authentication failure does not retry and pagehide cancels a pending retry', async () => {
    const unauthorized = browser({auth:async () => false}); await settle();
    assert.equal(unauthorized.calls.length, 0); assert.equal(unauthorized.timers.size, 0);
    const b = browser({request:() => ({error:'max clients reached'})}); await settle();
    assert.equal(b.timers.size, 1); b.events.get('pagehide')(); assert.equal(b.timers.size, 0);
});
