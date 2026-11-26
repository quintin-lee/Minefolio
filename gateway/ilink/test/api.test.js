'use strict';

const { test, describe } = require('node:test');
const assert = require('node:assert');
const { createGatewayApi, GatewayHttpError } = require('../api');

describe('ilink/api', () => {
  test('confirm posts code to the gateway confirm endpoint', async () => {
    const calls = [];
    const api = createGatewayApi({
      apiBase: 'http://backend.test',
      gatewayToken: 'gw-token',
      fetchImpl: async (url, init) => {
        calls.push({ url, init });
        return {
          status: 200,
          json: async () => ({ code: 0, data: { reply: '已记账：支出 ¥30.00' } }),
        };
      },
    });

    const res = await api.confirm(7, 'ilink:owner_wx', 'A7K2M9PQ');
    assert.deepStrictEqual(res, { reply: '已记账：支出 ¥30.00' });
    assert.strictEqual(calls.length, 1);
    assert.strictEqual(calls[0].url, 'http://backend.test/api/gateway/im/instances/7/confirm');
    assert.strictEqual(calls[0].init.method, 'POST');
    assert.strictEqual(calls[0].init.headers['X-Gateway-Token'], 'gw-token');
    assert.deepStrictEqual(JSON.parse(calls[0].init.body), {
      im_user_id: 'ilink:owner_wx',
      code: 'A7K2M9PQ',
    });
  });

  test('confirm surfaces backend error codes', async () => {
    const api = createGatewayApi({
      gatewayToken: 'gw-token',
      fetchImpl: async () => ({
        status: 200,
        json: async () => ({ code: 1004, message: '确认码无效、已过期或已使用' }),
      }),
    });
    await assert.rejects(
      () => api.confirm(7, 'ilink:owner_wx', 'ZZZZZZZZ'),
      (err) => err instanceof GatewayHttpError && err.code === 1004
    );
  });
});
