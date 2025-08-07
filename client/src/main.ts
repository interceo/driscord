const wsUrl = ((): string => {
  const host = location.hostname || 'localhost';
  const isHttps = location.protocol === 'https:';
  // По умолчанию сигналинг на 8080; можно переопределить через параметр ?ws_port=NNNN
  const url = new URL(location.href);
  const portParam = url.searchParams.get('ws_port');
  const port = portParam ? Number(portParam) : 8080;
  const proto = isHttps ? 'wss' : 'ws';
  return `${proto}://${host}:${port}`;
})();

const ws = new WebSocket(wsUrl);

const statusEl = document.getElementById('status')!;
const btnStart = document.getElementById('start') as HTMLButtonElement;
const btnStop = document.getElementById('stop') as HTMLButtonElement;
const sourceSel = document.getElementById('source') as HTMLSelectElement;
const videoLocal = document.getElementById('local') as HTMLVideoElement;
const videoRemote = document.getElementById('remote') as HTMLVideoElement;

let pc: RTCPeerConnection | null = null;
let localStream: MediaStream | null = null;
let peerId = Math.random().toString(36).slice(2);

ws.addEventListener('open', () => (statusEl.textContent = 'online'));
ws.addEventListener('close', () => (statusEl.textContent = 'offline'));
ws.addEventListener('message', async (ev) => {
  try {
    const data = JSON.parse(ev.data);
    if (data.sender === peerId) return; // ignore self
    if (!pc) return;

    if (data.type === 'offer') {
      await pc.setRemoteDescription(new RTCSessionDescription(data.sdp));
      const answer = await pc.createAnswer();
      await pc.setLocalDescription(answer);
      ws.send(JSON.stringify({ type: 'answer', sdp: pc.localDescription, sender: peerId }));
    } else if (data.type === 'answer') {
      await pc.setRemoteDescription(new RTCSessionDescription(data.sdp));
    } else if (data.type === 'ice') {
      await pc.addIceCandidate(data.candidate);
    }
  } catch (e) {
    console.error('ws message error', e);
  }
});

async function getStream(kind: 'camera' | 'screen'): Promise<MediaStream> {
  if (kind === 'screen') {
    // deno/lint-ignore no-explicit-any
    const gdm: any = (navigator.mediaDevices as any).getDisplayMedia;
    if (!gdm) throw new Error('getDisplayMedia unsupported');
    return await (navigator.mediaDevices as any).getDisplayMedia({ video: true, audio: false });
  }
  return await navigator.mediaDevices.getUserMedia({ video: true, audio: true });
}

function setupPeer() {
  pc = new RTCPeerConnection({
    iceServers: [
      { urls: 'stun:stun.l.google.com:19302' }
    ]
  });

  pc.onicecandidate = (ev) => {
    if (ev.candidate) {
      ws.send(JSON.stringify({ type: 'ice', candidate: ev.candidate, sender: peerId }));
    }
  };

  pc.ontrack = (ev) => {
    videoRemote.srcObject = ev.streams[0];
  };
}

async function start() {
  if (pc) return;
  setupPeer();

  const kind = sourceSel.value as 'camera' | 'screen';
  localStream = await getStream(kind);
  videoLocal.srcObject = localStream;
  localStream.getTracks().forEach((t) => pc!.addTrack(t, localStream!));

  const offer = await pc!.createOffer();
  await pc!.setLocalDescription(offer);
  ws.send(JSON.stringify({ type: 'offer', sdp: pc!.localDescription, sender: peerId }));

  btnStart.disabled = true;
  btnStop.disabled = false;
}

function stop() {
  if (!pc) return;
  pc.getSenders().forEach((s) => s.track && s.track.stop());
  pc.close();
  pc = null;
  if (localStream) {
    localStream.getTracks().forEach((t) => t.stop());
    localStream = null;
  }
  btnStart.disabled = false;
  btnStop.disabled = true;
}

btnStart.onclick = () => start().catch(console.error);
btnStop.onclick = () => stop(); 