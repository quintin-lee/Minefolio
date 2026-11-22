/**
 * Core gateway hardening unit tests:
 *  - platform whitelist enforcement (rejects path traversal / unknown platforms)
 *  - AbortSignal.timeout enforcement on slow backend endpoints
 */

const { test, before, after } = require('node:test');
const assert = require('node:assert');
const http = require('node:http');

const { handleImMessage, callImApi, pollImTask, TOKEN_CACHE } = require('../../index.js');

let server;
let apiBase;
let hitCount = 0;

before(async () => {
  server = http.createServer((req, res) => {
    hitCount += 1;
    if (req.url === '/api/im/qq/chat') {
      // Intentionally hang longer than test timeout to exercise AbortSignal.timeout
      setTimeout(() => {
        if (!res.writableEnded) {
          res.writeHead(200, { 'Content-Type': 'application/json' });
          res.end(JSON.stringify({ code: 0, data: { reply: 'late' } }));
        }
      }, 400);
      return;
    }
    if (req.url === '/api/im/tasks/99') {
      setTimeout(() => {
        if (!res.writableEnded) {
          res.writeHead(200, { 'Content-Type': 'application/json' });
          res.end(JSON.stringify({ code: 0, data: { status: 'completed', result: { reply: 'late' } } }));
        }
      }, 400);
      return;
    }
    res.writeHead(200, { 'Content-Type': 'application/json' });
    res.end(JSON.stringify({ code: 0, data: {} }));
  });
  await new Promise((r) => server.listen(0, r));
  apiBase = `http://localhost:${server.address().port}`;
  process.env.MINEFOLIO_API = apiBase;
});

after(() => server && server.close());

test('rejects invalid platforms before issuing any HTTP request', async () => {
  const beforeHits = hitCount;
  for (const bad of ['../admin', 'qq/../../users', 'discord', '', null, undefined, 'QQ']) {
    const r = await callImApi(bad, 'chat', { im_user_id: '1', message: 'hi' }, 'TOK');
    assert.strictEqual(r.code, 1002);
    assert.strictEqual(r.http, 400);

    const msg = await handleImMessage(bad, '1', 'hi', 'TOK');
    assert.match(msg, /不支持的 IM 平台/);
  }
  assert.strictEqual(hitCount, beforeHits);
});

test('aborts slow chat and task poll requests cleanly via AbortSignal.timeout', async () => {
  const prevTimeout = process.env.MINEFOLIO_HTTP_TIMEOUT_MS;
  process.env.MINEFOLIO_HTTP_TIMEOUT_MS = '80';
  try {
    const chatRes = await callImApi('qq', 'chat', { im_user_id: '1', message: 'hi' }, 'TOK');
    assert.strictEqual(chatRes.http, 504);
    assert.strictEqual(chatRes.code, 504);

    const pollRes = await pollImTask(99, 'TOK', { timeoutMs: 1000, intervalMs: 20 });
    assert.strictEqual(pollRes.http, 504);
    assert.strictEqual(pollRes.code, 504);
  } finally {
    if (prevTimeout === undefined) {
      delete process.env.MINEFOLIO_HTTP_TIMEOUT_MS;
    } else {
      process.env.MINEFOLIO_HTTP_TIMEOUT_MS = prevTimeout;
    }
  }
});

test('enforces bounded LRU capacity on TOKEN_CACHE', async () => {
  TOKEN_CACHE.clear();
  const prevMax = process.env.MINEFOLIO_GATEWAY_TOKEN_CACHE_MAX;
  process.env.MINEFOLIO_GATEWAY_TOKEN_CACHE_MAX = '2';
  try {
    await handleImMessage('qq', 'u1', '/bind C1');
    await handleImMessage('qq', 'u2', '/bind C2');
    await handleImMessage('qq', 'u3', '/bind C3');
    assert.strictEqual(TOKEN_CACHE.size, 2);
    assert.strictEqual(TOKEN_CACHE.has('qq:u1'), false);
    assert.strictEqual(TOKEN_CACHE.has('qq:u2'), true);
    assert.strictEqual(TOKEN_CACHE.has('qq:u3'), true);
  } finally {
    TOKEN_CACHE.clear();
    if (prevMax === undefined) {
      delete process.env.MINEFOLIO_GATEWAY_TOKEN_CACHE_MAX;
    } else {
      process.env.MINEFOLIO_GATEWAY_TOKEN_CACHE_MAX = prevMax;
    }
  }
});

test('expires stale entries in TOKEN_CACHE after TTL elapses', async () => {
  TOKEN_CACHE.clear();
  const prevTtl = process.env.MINEFOLIO_GATEWAY_TOKEN_CACHE_TTL_MS;
  process.env.MINEFOLIO_GATEWAY_TOKEN_CACHE_TTL_MS = '30';
  try {
    await handleImMessage('qq', 'ttl_user', '/bind TTL_TOK');
    assert.strictEqual(TOKEN_CACHE.has('qq:ttl_user'), true);
    await new Promise((r) => setTimeout(r, 45));
    const reply = await handleImMessage('qq', 'ttl_user', '查询资产');
    assert.match(reply, /尚未绑定/);
    assert.strictEqual(TOKEN_CACHE.has('qq:ttl_user'), false);
  } finally {
    TOKEN_CACHE.clear();
    if (prevTtl === undefined) {
      delete process.env.MINEFOLIO_GATEWAY_TOKEN_CACHE_TTL_MS;
    } else {
      process.env.MINEFOLIO_GATEWAY_TOKEN_CACHE_TTL_MS = prevTtl;
    }
  }
});
