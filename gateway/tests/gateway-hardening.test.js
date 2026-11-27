'use strict';

const { test, describe } = require('node:test');
const assert = require('node:assert');
const EventEmitter = require('events');
const http = require('http');
const fs = require('fs');
const os = require('os');
const path = require('path');

const { createGatewayApi, GatewayHttpError } = require('../ilink/api');
const { runLogin, buildRedirectBaseUrl, normalizeAllowedBaseUrl } = require('../ilink/login');
const { createSupervisor, DEFAULT_SHUTDOWN_GRACE_MS } = require('../ilink/supervisor');
const { createStateStore } = require('../ilink/state');
const {
  createOneBotServer,
  createOneBotTransport,
  buildWsUrl,
  computeBackoffMs,
  setupSignalHandlers,
} = require('../adapters/onebot-server');

describe('gateway hardening: ilink/api res.ok', () => {
  test('throws GatewayHttpError with status and body.error when !res.ok', async () => {
    const api = createGatewayApi({
      apiBase: 'http://backend.test',
      gatewayToken: 'gw-token',
      fetchImpl: async () => ({
        ok: false,
        status: 502,
        json: async () => ({ error: 'upstream bad gateway' }),
      }),
    });

    await assert.rejects(
      () => api.listInstances(),
      (err) =>
        err instanceof GatewayHttpError &&
        err.http === 502 &&
        err.message.includes('502') &&
        err.message.includes('upstream bad gateway')
    );
  });

  test('throws GatewayHttpError with status when !res.ok and body is non-JSON', async () => {
    const api = createGatewayApi({
      apiBase: 'http://backend.test',
      gatewayToken: 'gw-token',
      fetchImpl: async () => ({
        ok: false,
        status: 503,
        json: async () => {
          throw new SyntaxError('Unexpected token');
        },
      }),
    });

    await assert.rejects(
      () => api.listInstances(),
      (err) => err instanceof GatewayHttpError && err.http === 503 && err.message.includes('503')
    );
  });
});

describe('gateway hardening: ilink/login redirect allowlist', () => {
  test('buildRedirectBaseUrl allows only https weixin.qq.com and *.weixin.qq.com hosts', () => {
    assert.strictEqual(buildRedirectBaseUrl('sz.ilink.weixin.qq.com'), 'https://sz.ilink.weixin.qq.com');
    assert.strictEqual(buildRedirectBaseUrl('https://weixin.qq.com/'), 'https://weixin.qq.com');

    for (const bad of [
      '',
      null,
      undefined,
      'evil.com',
      'hk.ilink.test',
      'evilweixin.qq.com',
      'http://ilink.weixin.qq.com',
      'https://evil.com/weixin.qq.com',
      'weixin.qq.com@evil.com',
      'ilink.weixin.qq.com:8443',
      'ilink.weixin.qq.com/path',
    ]) {
      assert.strictEqual(buildRedirectBaseUrl(bad), null, `expected null for ${bad}`);
    }
  });

  test('runLogin rejects non-weixin redirect_host and statusRes.baseurl', async () => {
    const tmpRoot = fs.mkdtempSync(path.join(os.tmpdir(), 'mf-ilink-allowlist-'));
    try {
      const state = createStateStore({ rootDir: tmpRoot, instanceId: 99 });
      const usedBaseUrls = [];
      let step = 0;

      const client = {
        getBotQrcode: async () => ({ qrcode: 'q_allow', qrcode_img_content: 'qr_allow' }),
        getQrcodeStatus: async ({ baseUrl }) => {
          usedBaseUrls.push(baseUrl || 'default');
          step += 1;
          if (step === 1) {
            return { status: 'scaned_but_redirect', redirect_host: 'attacker.example.com' };
          }
          if (step === 2) {
            return { status: 'scaned_but_redirect', redirect_host: 'hk.ilink.weixin.qq.com' };
          }
          return {
            status: 'confirmed',
            bot_token: 'tok_allow',
            ilink_bot_id: 'bot_allow',
            ilink_user_id: 'user_allow',
            baseurl: 'http://evilweixin.qq.com',
          };
        },
      };

      const res = await runLogin({ client, state });
      assert.strictEqual(res.ok, true);
      assert.deepStrictEqual(usedBaseUrls, ['default', 'default', 'https://hk.ilink.weixin.qq.com']);
      assert.strictEqual(res.account.baseurl, 'https://hk.ilink.weixin.qq.com');
      assert.strictEqual(normalizeAllowedBaseUrl('https://sz.ilink.weixin.qq.com/'), 'https://sz.ilink.weixin.qq.com');
      assert.strictEqual(normalizeAllowedBaseUrl('http://ilink.weixin.qq.com'), null);
    } finally {
      fs.rmSync(tmpRoot, { recursive: true, force: true });
    }
  });
});

describe('gateway hardening: ilink/supervisor shutdown grace', () => {
  test('defaults shutdown grace period to 3500ms', () => {
    assert.strictEqual(DEFAULT_SHUTDOWN_GRACE_MS, 3500);
  });

  test('waits for child exit before SIGKILL and skips SIGKILL when child exits gracefully', async () => {
    const tmpRoot = fs.mkdtempSync(path.join(os.tmpdir(), 'mf-ilink-grace-'));
    try {
      const signals = [];
      const sup = createSupervisor({
        api: { listInstances: async () => [], updateStatus: async () => {} },
        stateRoot: tmpRoot,
        shutdownGraceMs: 250,
        spawnImpl: () => {
          const proc = new EventEmitter();
          proc.kill = (sig) => {
            signals.push(sig);
            if (sig === 'SIGTERM') {
              setTimeout(() => proc.emit('exit', 0), 20);
            }
          };
          return proc;
        },
        killImpl: (proc, sig) => proc.kill(sig),
      });

      await sup.reconcile([{ id: 1, status: 'active', login_epoch: 0, enabled: 1 }]);
      const t0 = Date.now();
      await sup.shutdown();
      const elapsed = Date.now() - t0;

      assert.deepStrictEqual(signals, ['SIGTERM']);
      assert.ok(elapsed < 200, `expected early return on child exit, took ${elapsed}ms`);
    } finally {
      fs.rmSync(tmpRoot, { recursive: true, force: true });
    }
  });

  test('sends SIGKILL only after grace window expires for unresponsive child', async () => {
    const tmpRoot = fs.mkdtempSync(path.join(os.tmpdir(), 'mf-ilink-sigkill-'));
    try {
      const signals = [];
      const sup = createSupervisor({
        api: { listInstances: async () => [], updateStatus: async () => {} },
        stateRoot: tmpRoot,
        shutdownGraceMs: 220,
        spawnImpl: () => {
          const proc = new EventEmitter();
          proc.kill = (sig) => {
            signals.push({ sig, at: Date.now() });
          };
          return proc;
        },
        killImpl: (proc, sig) => proc.kill(sig),
      });

      await sup.reconcile([{ id: 2, status: 'active', login_epoch: 0, enabled: 1 }]);
      await sup.shutdown();

      assert.strictEqual(signals.length, 2);
      assert.strictEqual(signals[0].sig, 'SIGTERM');
      assert.strictEqual(signals[1].sig, 'SIGKILL');
      assert.ok(
        signals[1].at - signals[0].at >= 180,
        `expected SIGKILL after grace window (~220ms), got ${signals[1].at - signals[0].at}ms`
      );
    } finally {
      fs.rmSync(tmpRoot, { recursive: true, force: true });
    }
  });
});

describe('gateway hardening: onebot-server WS auth, timeout, reconnect, heartbeat & shutdown', () => {
  test('passes ONEBOT_ACCESS_TOKEN in WS URL query and Authorization header', async () => {
    let seenUrl = '';
    let seenAuth = '';
    const srv = http.createServer();
    srv.on('upgrade', (req, sock) => {
      seenUrl = req.url;
      seenAuth = req.headers.authorization || '';
      sock.destroy();
    });
    await new Promise((r) => srv.listen(0, '127.0.0.1', r));
    const port = srv.address().port;

    try {
      const built = buildWsUrl(`ws://127.0.0.1:${port}`, 'secret-tok');
      assert.strictEqual(built, `ws://127.0.0.1:${port}/?access_token=secret-tok`);

      const server = createOneBotServer({
        wsUrl: `ws://127.0.0.1:${port}`,
        apiUrl: `http://127.0.0.1:${port}`,
        accessToken: 'secret-tok',
        bindToken: 'bind-tok',
        baseReconnectMs: 5000,
        heartbeatIntervalMs: 0,
        log: { info: () => {}, warn: () => {}, error: () => {} },
      });
      server.connect();

      await new Promise((r) => setTimeout(r, 80));
      server.stop();

      assert.strictEqual(seenUrl, '/?access_token=secret-tok');
      assert.strictEqual(seenAuth, 'Bearer secret-tok');
    } finally {
      await new Promise((r) => srv.close(r));
    }
  });

  test('sendPrivateMsg passes AbortSignal timeout to fetch', async () => {
    let seenInit = null;
    const transport = createOneBotTransport({
      apiUrl: 'http://onebot.test',
      accessToken: 'tok123',
      fetchTimeoutMs: 15000,
      fetchImpl: async (_url, init) => {
        seenInit = init;
        return {
          ok: true,
          status: 200,
          json: async () => ({ status: 'ok', retcode: 0 }),
        };
      },
    });

    await transport.sendPrivateMsg('10001', 'hello');
    assert.ok(seenInit, 'expected fetch to be called');
    assert.strictEqual(seenInit.headers.Authorization, 'Bearer tok123');
    assert.ok(seenInit.signal instanceof AbortSignal, 'expected AbortSignal on fetch init');
  });

  test('closes previous socket before reconnect and uses exponential backoff with jitter', async () => {
    const sockets = [];
    class FakeWs extends EventEmitter {
      constructor(url, opts) {
        super();
        this.url = url;
        this.opts = opts;
        this.readyState = 1;
        this.closed = false;
        this.pingCount = 0;
        sockets.push(this);
      }
      ping() {
        this.pingCount += 1;
      }
      send() {}
      close() {
        this.closed = true;
        this.readyState = 3;
      }
    }

    const d0 = computeBackoffMs(0, { baseDelayMs: 1000, maxDelayMs: 30000, randomFn: () => 0.5 });
    const d1 = computeBackoffMs(1, { baseDelayMs: 1000, maxDelayMs: 30000, randomFn: () => 0.5 });
    const d2 = computeBackoffMs(2, { baseDelayMs: 1000, maxDelayMs: 30000, randomFn: () => 0.5 });
    assert.ok(d1 > d0 && d2 > d1, `expected exponential growth: ${d0}, ${d1}, ${d2}`);
    assert.notStrictEqual(
      computeBackoffMs(1, { baseDelayMs: 1000, maxDelayMs: 30000, randomFn: () => 0 }),
      computeBackoffMs(1, { baseDelayMs: 1000, maxDelayMs: 30000, randomFn: () => 1 })
    );

    const server = createOneBotServer({
      wsUrl: 'ws://127.0.0.1:3001',
      apiUrl: 'http://127.0.0.1:3001',
      accessToken: 'tok',
      WebSocketImpl: FakeWs,
      baseReconnectMs: 25,
      maxReconnectMs: 100,
      heartbeatIntervalMs: 30,
      log: { info: () => {}, warn: () => {}, error: () => {} },
    });

    server.connect();
    assert.strictEqual(sockets.length, 1);
    const first = sockets[0];
    first.emit('open');

    // Wait for at least one ping, then miss pong so heartbeat closes stale socket and reconnects
    await new Promise((r) => setTimeout(r, 110));
    assert.ok(first.pingCount >= 1, 'expected heartbeat ping on socket');
    assert.strictEqual(first.closed, true, 'expected stale socket to be closed');
    assert.ok(sockets.length >= 2, 'expected reconnect to spawn new socket');

    // Also verify manual connect() closes previous socket first
    const prevLatest = sockets[sockets.length - 1];
    server.connect();
    assert.strictEqual(prevLatest.closed, true);

    server.stop();
  });

  test('supports heartbeat when WebSocket lacks .ping() (WHATWG ws-shim) and cleans up on SIGTERM/SIGINT', async () => {
    const sentFrames = [];
    class WhatwgLikeWs extends EventEmitter {
      constructor() {
        super();
        this.readyState = 1;
        this.closed = false;
      }
      send(payload) {
        sentFrames.push(payload);
      }
      close() {
        this.closed = true;
        this.readyState = 3;
      }
    }

    const fakeProc = new EventEmitter();
    const exits = [];
    const server = createOneBotServer({
      wsUrl: 'ws://127.0.0.1:3001',
      apiUrl: 'http://127.0.0.1:3001',
      WebSocketImpl: WhatwgLikeWs,
      heartbeatIntervalMs: 20,
      baseReconnectMs: 500,
      log: { info: () => {}, warn: () => {}, error: () => {} },
    });
    setupSignalHandlers(server, fakeProc, (code) => exits.push(code));

    server.connect();
    server.getSocket().emit('open');
    await new Promise((r) => setTimeout(r, 30));
    assert.ok(sentFrames.length >= 1, 'expected fallback get_status heartbeat frame');

    fakeProc.emit('SIGTERM');
    assert.deepStrictEqual(exits, [0]);
    assert.strictEqual(server.getSocket(), null);
  });
});
