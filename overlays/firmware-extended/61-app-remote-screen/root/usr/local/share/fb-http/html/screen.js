// SPDX-License-Identifier: GPL-3.0-or-later
const video = document.getElementById('screen');
const statusText = document.getElementById('status');
let peer, channel, session, generation = 0, pressed = false;
async function signal(message) {
    const response = await fetch('webrtc', {method:'POST',
        headers:{'Content-Type':'application/json', ...getAuthHeaders()}, body:JSON.stringify(message)});
    if (!response.ok) throw new Error(`连接失败 (${response.status})`);
    const result = await response.json();
    if (result.error) throw new Error(result.error);
    return result;
}
function release() {
    if (pressed && channel?.readyState === 'open') channel.send(JSON.stringify({touch:'up'}));
    pressed = false;
}
function closeConnection() {
    release(); channel?.close(); peer?.close();
    channel = null; peer = null; session = null; video.srcObject = null;
}
async function connect() {
    const current = ++generation;
    closeConnection(); statusText.textContent = '正在连接…';
    try {
        if (!await initAuth()) throw new Error('请先登录打印机');
        const offer = await signal({type:'request', keepAlive:true});
        if (current !== generation) return;
        session = offer.id;
        const id = session;
        const pc = new RTCPeerConnection({iceServers:offer.iceServers || []}); peer = pc;
        pc.ontrack = event => { if (current === generation) video.srcObject = new MediaStream([event.track]); };
        pc.ondatachannel = event => {
            if (current !== generation) return;
            channel = event.channel;
            channel.onmessage = event => { if (event.data === 'ping') channel.send('pong'); };
        };
        pc.onicecandidate = event => {
            if (event.candidate) signal({type:'remote_candidate', id, candidates:[event.candidate]}).catch(()=>{});
        };
        pc.onconnectionstatechange = () => {
            if (current !== generation) return;
            statusText.textContent = pc.connectionState === 'connected' ? '已连接' : '连接已断开，请重新连接';
            if (['failed','closed','disconnected'].includes(pc.connectionState)) release();
        };
        await pc.setRemoteDescription({type:'offer',sdp:offer.sdp});
        await pc.setLocalDescription(await pc.createAnswer());
        await signal({type:'answer',id,sdp:pc.localDescription.sdp});
    } catch (error) {
        if (current === generation) { closeConnection(); statusText.textContent = error.message; }
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
}
video.addEventListener('pointerdown',e=>{if(e.isPrimary && e.button===0)pointer(e,'down');});
video.addEventListener('pointermove',e=>{if(e.isPrimary)pointer(e,'move');});
video.addEventListener('pointerup',release);
video.addEventListener('pointercancel',release);
video.addEventListener('lostpointercapture',release);
window.addEventListener('blur',release);
window.addEventListener('pagehide',()=>{++generation;closeConnection();});
document.getElementById('reconnect').onclick = connect;
document.getElementById('fullscreen').onclick = () => document.fullscreenElement ? document.exitFullscreen() : document.documentElement.requestFullscreen();
connect();
