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

const DEFAULT_FETCH_TIMEOUT_MS = 15000;
const DEFAULT_BASE_RECONNECT_MS = 1000;
const DEFAULT_MAX_RECONNECT_MS = 30000;
const DEFAULT_HEARTBEAT_INTERVAL_MS = 30000;

function parseBindMap(raw) {
  if (!raw) return {};
  try {
    const parsed = JSON.parse(raw);
    return parsed && typeof parsed === 'object' ? parsed : {};
  } catch {
    return {};
  }
}

function buildWsUrl(wsUrl, accessToken) {
  if (!accessToken) return wsUrl;
  try {
    const u = new URL(wsUrl);
    if (!u.searchParams.has('access_token')) {
      u.searchParams.set('access_token', accessToken);
    }
    return u.toString();
  } catch {
    return wsUrl;
  }
}

function computeBackoffMs(
  attempt,
  {
    baseDelayMs = DEFAULT_BASE_RECONNECT_MS,
    maxDelayMs = DEFAULT_MAX_RECONNECT_MS,
    randomFn = Math.random,
  } = {}
) {
  const exp = Math.min(maxDelayMs, baseDelayMs * 2 ** Math.max(0, attempt));
  const factor = 0.75 + randomFn() * 0.5;
  return Math.max(1, Math.round(exp * factor));
}

/** OneBot v11 HTTP reply channel (send_private_msg action). */
function createOneBotTransport({
  apiUrl,
  accessToken = '',
  fetchImpl = fetch,
  fetchTimeoutMs = DEFAULT_FETCH_TIMEOUT_MS,
  log = console,
}) {
  return {
    async sendPrivateMsg(uid, text) {
      const base = String(apiUrl).replace(/\/+$/, '');
      const url = base.endsWith('/send_private_msg') ? base : `${base}/send_private_msg`;
      const body = JSON.stringify({
        action: 'send_private_msg',
        params: { user_id: Number(uid), message: text },
        user_id: Number(uid),
        message: text,
      });
      const headers = { 'Content-Type': 'application/json' };
      if (accessToken) headers['Authorization'] = `Bearer ${accessToken}`;
      try {
        const res = await fetchImpl(url, {
          method: 'POST',
          headers,
          body,
          signal: AbortSignal.timeout(fetchTimeoutMs),
        });
        if (!res.ok) {
          log.warn?.(`[onebot-server] send_private_msg HTTP ${res.status} for ${uid}`);
        } else {
          const json = await res.json().catch(() => null);
          if (json && json.status !== 'ok' && json.retcode !== 0) {
            log.warn?.(
              `[onebot-server] send_private_msg failed: retcode=${json.retcode} message=${json.message}`
            );
          }
        }
      } catch (err) {
        log.warn?.(`[onebot-server] send_private_msg error for ${uid}:`, err && err.message);
      }
    },
  };
}

function createOneBotServer({
  wsUrl,
  apiUrl,
  accessToken = '',
  bindToken = '',
  bindMap = {},
  WebSocketImpl = WebSocket,
  fetchImpl = fetch,
  fetchTimeoutMs = DEFAULT_FETCH_TIMEOUT_MS,
  baseReconnectMs = DEFAULT_BASE_RECONNECT_MS,
  maxReconnectMs = DEFAULT_MAX_RECONNECT_MS,
  heartbeatIntervalMs = DEFAULT_HEARTBEAT_INTERVAL_MS,
  randomFn = Math.random,
  log = console,
}) {
  const logInfo = (...args) => (log.log || log.info || console.log).apply(log, args);

  async function getBindToken(imUserId) {
    if (bindMap[imUserId]) return bindMap[imUserId];
    return bindToken;
  }

  const transport = createOneBotTransport({
    apiUrl,
    accessToken,
    fetchImpl,
    fetchTimeoutMs,
    log,
  });
  const adapter = createOneBotAdapter({ getBindToken, transport, log });

  let ws = null;
  let reconnectTimer = null;
  let heartbeatTimer = null;
  let reconnectAttempt = 0;
  let stopped = false;
  let isAlive = false;

  function clearHeartbeat() {
    if (heartbeatTimer) {
      clearInterval(heartbeatTimer);
      heartbeatTimer = null;
    }
  }

  function closeSocket(sock) {
    if (!sock) return;
    clearHeartbeat();
    try {
      if (typeof sock.terminate === 'function') {
        sock.terminate();
      } else if (typeof sock.close === 'function') {
        sock.close();
      }
    } catch {
      // Ignore errors while closing stale/already-closed sockets
    }
  }

  function startHeartbeat(sock) {
    clearHeartbeat();
    if (!heartbeatIntervalMs || heartbeatIntervalMs <= 0) return;
    isAlive = true;
    heartbeatTimer = setInterval(() => {
      if (stopped || ws !== sock) {
        clearHeartbeat();
        return;
      }
      if (!isAlive) {
        log.warn?.('[onebot-server] WS heartbeat timeout, closing stale socket');
        ws = null;
        closeSocket(sock);
        scheduleReconnect();
        return;
      }
      isAlive = false;
      try {
        if (typeof sock.ping === 'function') {
          sock.ping();
        } else if (
          typeof sock.send === 'function' &&
          (sock.readyState === undefined || sock.readyState === 1)
        ) {
          sock.send(JSON.stringify({ action: 'get_status', echo: 'onebot-heartbeat' }));
        }
      } catch {
        // Ignore send errors; close/error event or next tick will trigger reconnect
      }
    }, heartbeatIntervalMs);
  }

  function scheduleReconnect() {
    if (stopped || reconnectTimer) return;
    const delayMs = computeBackoffMs(reconnectAttempt, {
      baseDelayMs: baseReconnectMs,
      maxDelayMs: maxReconnectMs,
      randomFn,
    });
    reconnectAttempt += 1;
    log.warn?.(`[onebot-server] WS disconnected or unavailable, reconnecting in ${delayMs}ms...`);
    reconnectTimer = setTimeout(() => {
      reconnectTimer = null;
      if (!stopped) {
        connect();
      }
    }, delayMs);
  }

  function connect() {
    if (stopped) return;
    if (ws) {
      const prev = ws;
      ws = null;
      closeSocket(prev);
    }
    clearHeartbeat();

    const targetUrl = buildWsUrl(wsUrl, accessToken);
    const wsOptions = accessToken
      ? { headers: { Authorization: `Bearer ${accessToken}` } }
      : undefined;

    let sock;
    try {
      sock = wsOptions ? new WebSocketImpl(targetUrl, wsOptions) : new WebSocketImpl(targetUrl);
    } catch (err) {
      log.error?.('[onebot-server] WebSocket creation error:', err && err.message);
      scheduleReconnect();
      return;
    }

    ws = sock;

    sock.on('open', () => {
      if (stopped || ws !== sock) return;
      reconnectAttempt = 0;
      logInfo(`[onebot-server] Connected to OneBot WebSocket @ ${wsUrl}`);
      startHeartbeat(sock);
    });

    sock.on('pong', () => {
      if (stopped || ws !== sock) return;
      isAlive = true;
    });

    sock.on('message', (data) => {
      if (stopped || ws !== sock) return;
      isAlive = true;
      let event;
      try {
        const raw = typeof data === 'string' ? data : data.toString();
        event = JSON.parse(raw);
      } catch {
        return;
      }
      adapter
        .onEvent(event)
        .catch((e) => log.error?.('[onebot-server] onEvent error', e && e.message));
    });

    sock.on('close', () => {
      if (ws !== sock) return;
      ws = null;
      clearHeartbeat();
      if (!stopped) {
        scheduleReconnect();
      }
    });

    sock.on('error', (e) => {
      if (ws !== sock) return;
      log.error?.('[onebot-server] WS error', e && e.message);
      ws = null;
      closeSocket(sock);
      if (!stopped) {
        scheduleReconnect();
      }
    });
  }

  function stop() {
    stopped = true;
    if (reconnectTimer) {
      clearTimeout(reconnectTimer);
      reconnectTimer = null;
    }
    clearHeartbeat();
    if (ws) {
      const prev = ws;
      ws = null;
      closeSocket(prev);
    }
  }

  return {
    connect,
    stop,
    getSocket: () => ws,
    transport,
    adapter,
  };
}

function setupSignalHandlers(server, proc = process, exitFn = (code) => process.exit(code)) {
  const onSignal = (sig) => {
    console.log(`[onebot-server] Received ${sig}, shutting down...`);
    server.stop();
    exitFn(0);
  };
  proc.on('SIGTERM', () => onSignal('SIGTERM'));
  proc.on('SIGINT', () => onSignal('SIGINT'));
}

if (require.main === module) {
  const API_BASE = process.env.MINEFOLIO_API || 'http://localhost:8080';
  const ONEBOT_WS_URL = process.env.ONEBOT_WS_URL;
  const ONEBOT_API_URL = process.env.ONEBOT_API_URL;
  const ONEBOT_ACCESS_TOKEN = process.env.ONEBOT_ACCESS_TOKEN || '';
  const BIND_TOKEN = process.env.ONEBOT_BIND_TOKEN || '';
  const BIND_MAP = parseBindMap(process.env.ONEBOT_BIND_MAP);

  if (!ONEBOT_WS_URL || !ONEBOT_API_URL) {
    console.error(
      'OneBot gateway needs ONEBOT_WS_URL (inbound WS) and ONEBOT_API_URL (outbound HTTP).'
    );
    console.error(
      'Example: ONEBOT_WS_URL=ws://127.0.0.1:3001 ONEBOT_API_URL=http://127.0.0.1:3001 ONEBOT_BIND_TOKEN=...'
    );
    process.exit(2);
  }

  const server = createOneBotServer({
    wsUrl: ONEBOT_WS_URL,
    apiUrl: ONEBOT_API_URL,
    accessToken: ONEBOT_ACCESS_TOKEN,
    bindToken: BIND_TOKEN,
    bindMap: BIND_MAP,
  });
  setupSignalHandlers(server);

  console.log(`[onebot-server] QQ gateway bridging OneBot @ ${ONEBOT_WS_URL} -> ${API_BASE}`);
  server.connect();
}

module.exports = {
  createOneBotServer,
  createOneBotTransport,
  buildWsUrl,
  computeBackoffMs,
  setupSignalHandlers,
};
