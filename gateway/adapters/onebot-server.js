#!/usr/bin/env node
/**
 * Runnable OneBot v11 (QQ) gateway service.
 *
 * Connects to a NapCat / LLObot / go-cqhttp OneBot endpoint:
 *   - inbound : OneBot WebSocket (ONEBOT_WS_URL) event stream
 *   - outbound: OneBot HTTP API  (ONEBOT_API_URL + ONEBOT_ACCESS_TOKEN) send_private_msg
 *
 * bind-token resolution (P1, static; P2-A will replace with a backend
 * /im/{platform}/bind-auth call):
 *   ONEBOT_BIND_TOKEN  a single im_bind token shared by all bridged QQ users
 *   ONEBOT_BIND_MAP    JSON { "<uid>": "<im_bind token>", ... } per-user override
 *
 * Zero dependencies beyond Node >= 18 built-ins (WebSocket, fetch).
 */

const { WebSocket } = require('./ws-shim.js');
const { createOneBotAdapter } = require('./onebot.js');

const API_BASE = process.env.MINEFOLIO_API || 'http://localhost:8080';
const ONEBOT_WS_URL = process.env.ONEBOT_WS_URL;
const ONEBOT_API_URL = process.env.ONEBOT_API_URL;
const ONEBOT_ACCESS_TOKEN = process.env.ONEBOT_ACCESS_TOKEN || '';
const BIND_TOKEN = process.env.ONEBOT_BIND_TOKEN || '';
let BIND_MAP = {};
try {
  if (process.env.ONEBOT_BIND_MAP) BIND_MAP = JSON.parse(process.env.ONEBOT_BIND_MAP);
} catch {
  BIND_MAP = {};
}

if (!ONEBOT_WS_URL || !ONEBOT_API_URL) {
  console.error('OneBot gateway needs ONEBOT_WS_URL (inbound WS) and ONEBOT_API_URL (outbound HTTP).');
  console.error('Example: ONEBOT_WS_URL=ws://127.0.0.1:3001 ONEBOT_API_URL=http://127.0.0.1:3001 ONEBOT_BIND_TOKEN=...');
  process.exit(2);
}

/** P1 static bind-token resolver; P2-A swaps this for a backend call. */
async function getBindToken(imUserId) {
  if (BIND_MAP[imUserId]) return BIND_MAP[imUserId];
  return BIND_TOKEN;
}

/** OneBot v11 HTTP reply channel (send_private_msg action). */
const transport = {
  async sendPrivateMsg(uid, text) {
    const body = JSON.stringify({
      action: 'send_private_msg',
      params: { user_id: Number(uid), message: text },
    });
    const headers = { 'Content-Type': 'application/json' };
    if (ONEBOT_ACCESS_TOKEN) headers['Authorization'] = `Bearer ${ONEBOT_ACCESS_TOKEN}`;
    const res = await fetch(ONEBOT_API_URL, { method: 'POST', headers, body });
    if (!res.ok) {
      console.warn(`[onebot-server] send_private_msg ${res.status} for ${uid}`);
    }
  },
};

const adapter = createOneBotAdapter({ getBindToken, transport });

let ws;
let reconnectTimer = null;

function scheduleReconnect() {
  if (reconnectTimer) return;
  console.warn('[onebot-server] WS disconnected or unavailable, reconnecting in 3s...');
  reconnectTimer = setTimeout(() => {
    reconnectTimer = null;
    connect();
  }, 3000);
}

function connect() {
  try {
    ws = new WebSocket(ONEBOT_WS_URL);
  } catch (err) {
    console.error('[onebot-server] WebSocket creation error:', err && err.message);
    scheduleReconnect();
    return;
  }

  ws.on('open', () => {
    console.log(`[onebot-server] Connected to OneBot WebSocket @ ${ONEBOT_WS_URL}`);
  });

  ws.on('message', (data) => {
    let event;
    try {
      event = JSON.parse(data.toString());
    } catch {
      return;
    }
    adapter.onEvent(event).catch((e) => console.error('[onebot-server] onEvent error', e && e.message));
  });

  ws.on('close', () => {
    scheduleReconnect();
  });

  ws.on('error', (e) => {
    console.error('[onebot-server] WS error', e && e.message);
    scheduleReconnect();
  });
}

console.log(`[onebot-server] QQ gateway bridging OneBot @ ${ONEBOT_WS_URL} -> ${API_BASE}`);
connect();
