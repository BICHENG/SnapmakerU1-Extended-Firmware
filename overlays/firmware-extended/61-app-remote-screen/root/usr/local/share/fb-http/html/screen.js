// SPDX-License-Identifier: GPL-3.0-or-later
const video = document.getElementById('screen');
const statusText = document.getElementById('status');
let peer, channel, session, generation = 0, pressed = false, retryTimer, holdTimer, retries = 0;
const retryDelays = [1000, 2000, 4000, 8000, 15000];
const identityKey = 'u1-screen-client';
const randomOwner = () => Array.from(crypto.getRandomValues(new Uint8Array(16)), n => n.toString(16).padStart(2, '0')).join('');
let identity = {owner:randomOwner(), sequence:0};
try {
    // Fluidd embeds this page: its top-level reload is the navigation that matters.
    const navigation = window.performance.getEntriesByType('navigation')[0];
    const parentNavigation = window.top.performance.getEntriesByType('navigation')[0];
    const saved = JSON.parse(sessionStorage.getItem(identityKey));
    if ((navigation?.type === 'reload' || parentNavigation?.type === 'reload') &&
        /^[0-9a-f]{32}$/.test(saved?.owner) && Number.isSafeInteger(saved.sequence) && saved.sequence >= 0)
        identity = saved;
} catch (_) {}
function nextIdentity() {
    if (identity.sequence >= Number.MAX_SAFE_INTEGER) identity = {owner:randomOwner(), sequence:0};
    ++identity.sequence;
    try { sessionStorage.setItem(identityKey, JSON.stringify(identity)); } catch (_) {}
    return {screen_owner:identity.owner, screen_generation:identity.sequence};
}
async function signal(message, keepalive = false) {
    const response = await fetch('webrtc', {method:'POST', keepalive,
        headers:{'Content-Type':'application/json', ...getAuthHeaders()}, body:JSON.stringify(message)});
    if (!response.ok) {
        const error = new Error(`连接失败 (${response.status})`);
        error.retryable = response.status >= 500 || response.status === 429;
        throw error;
    }
    const result = await response.json();
    if (result.error) {
        const error = new Error(result.error);
        error.retryable = ['max clients reached', 'client not found'].includes(result.error);
        throw error;
    }
    return result;
}
function release() {
    clearInterval(holdTimer); holdTimer = null;
    if (pressed && channel?.readyState === 'open') {
        try { channel.send(JSON.stringify({touch:'up'})); } catch (_) {}
    }
    pressed = false;
}
function closeSession(id) {
    return id ? signal({type:'close', id}, true).catch(()=>{}) : Promise.resolve();
}
function closeConnection() {
    const id = session;
    release();
    if (peer) peer.onconnectionstatechange = null;
    channel?.close(); peer?.close();
    channel = null; peer = null; session = null; video.srcObject = null;
    return closeSession(id);
}
function retryConnection(error) {
    if (error.retryable === false || retries >= retryDelays.length) {
        statusText.textContent = `${error.message}，请重新连接`;
        return;
    }
    statusText.textContent = '连接中断，正在重新连接…';
    const expected = generation;
    retryTimer = setTimeout(()=>{if (generation === expected) connect(false);}, retryDelays[retries++]);
}
async function connect(manual = true) {
    const current = ++generation;
    clearTimeout(retryTimer);
    if (manual) retries = 0;
    const closing = closeConnection();
    statusText.textContent = '正在连接…';
    let id, pc, negotiated = false;
    try {
        const authorized = await initAuth();
        if (current !== generation) return;
        if (!authorized) {
            const error = new Error('请先登录打印机'); error.retryable = false; throw error;
        }
        await closing;
        if (current !== generation) return;
        const offer = await signal({type:'request', keepAlive:true, ...nextIdentity()});
        id = offer.id;
        if (current !== generation) return;
        session = id;
        pc = new RTCPeerConnection({iceServers:offer.iceServers || []}); peer = pc;
        pc.ontrack = event => { if (current === generation) video.srcObject = new MediaStream([event.track]); };
        pc.ondatachannel = event => {
            if (current !== generation) return;
            const dc = event.channel; channel = dc;
            dc.onmessage = event => { if (event.data === 'ping' && dc.readyState === 'open') dc.send('pong'); };
        };
        pc.onicecandidate = event => {
            if (current === generation && event.candidate)
                signal({type:'remote_candidate', id, candidates:[event.candidate]}).catch(()=>{});
        };
        pc.onconnectionstatechange = () => {
            if (current !== generation) return;
            if (pc.connectionState === 'connected') {
                statusText.textContent = '已连接'; retries = 0;
            } else if (['failed','closed','disconnected'].includes(pc.connectionState)) {
                ++generation;
                closeConnection();
                retryConnection(new Error('连接已断开'));
            }
        };
        await pc.setRemoteDescription({type:'offer',sdp:offer.sdp});
        if (current !== generation) return;
        const answer = await pc.createAnswer();
        if (current !== generation) return;
        await pc.setLocalDescription(answer);
        if (current !== generation) return;
        await signal({type:'answer',id,sdp:pc.localDescription.sdp});
        if (current !== generation) return;
        negotiated = true;
    } catch (error) {
        if (current === generation) {
            ++generation;
            closeConnection();
            retryConnection(error);
        }
    } finally {
        if (!negotiated && id) {
            if (pc) { pc.onconnectionstatechange = null; pc.close(); }
            closeSession(id);
        }
    }
}
function pointer(event, action) {
    if (channel?.readyState !== 'open') return;
    const box = video.getBoundingClientRect();
    const scale = Math.min(box.width / 480, box.height / 320);
    const width = 480 * scale, height = 320 * scale;
    const x = (event.clientX - box.left - (box.width-width)/2)/width;
    const y = (event.clientY - box.top - (box.height-height)/2)/height;
    if (action === 'down' && (x < 0 || x > 1 || y < 0 || y > 1)) return;
    if (action === 'down') { pressed = true; video.setPointerCapture(event.pointerId); }
    if (!pressed) return;
    channel.send(JSON.stringify({touch:action,x:Math.max(0,Math.min(1,x)),y:Math.max(0,Math.min(1,y))}));
    if (action === 'down') {
        clearInterval(holdTimer);
        holdTimer = setInterval(()=>{
            if (!pressed || channel?.readyState !== 'open') { release(); return; }
            try { channel.send(JSON.stringify({touch:'hold'})); } catch (_) { release(); }
        }, 500);
    }
}
video.addEventListener('pointerdown',e=>{if(e.isPrimary && e.button===0)pointer(e,'down');});
video.addEventListener('pointermove',e=>{if(e.isPrimary)pointer(e,'move');});
video.addEventListener('pointerup',release);
video.addEventListener('pointercancel',release);
video.addEventListener('lostpointercapture',release);
window.addEventListener('blur',release);
window.addEventListener('pagehide',()=>{++generation;clearTimeout(retryTimer);closeConnection();});
window.addEventListener('pageshow',event=>{if(event.persisted)connect();});
document.getElementById('reconnect').onclick = ()=>connect();
document.getElementById('fullscreen').onclick = () => document.fullscreenElement ? document.exitFullscreen() : document.documentElement.requestFullscreen();
connect();
