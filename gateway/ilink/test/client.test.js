'use strict';

const { test, describe } = require('node:test');
const assert = require('node:assert');

const {
  APP_ID,
  CLIENT_VERSION,
  CHANNEL_VERSION,
  BOT_AGENT,
  IlinkSessionExpiredError,
  IlinkContextInvalidError,
  IlinkTimeoutError,
  buildHeaders,
  baseInfo,
  classifyPayloadError,
  createIlinkClient,
} = require('../client');

describe('ilink/client', () => {
  test('buildHeaders sets common iLink headers and optional bearer token', () => {
    const common = buildHeaders({ commonOnly: true });
    assert.strictEqual(common['iLink-App-Id'], APP_ID);
    assert.strictEqual(common['iLink-App-ClientVersion'], CLIENT_VERSION);
    assert.strictEqual(common['User-Agent'], BOT_AGENT);
    assert.ok(common['X-WECHAT-UIN']);
    assert.strictEqual(common.Authorization, undefined);
    assert.strictEqual(common.AuthorizationType, undefined);

    const authed = buildHeaders({ token: 'secret-tok', bodyBytes: 42 });
    assert.strictEqual(authed.AuthorizationType, 'ilink_bot_token');
    assert.strictEqual(authed.Authorization, 'Bearer secret-tok');
    assert.strictEqual(authed['Content-Length'], '42');
    assert.deepStrictEqual(baseInfo(), { channel_version: CHANNEL_VERSION });
  });

  test('getBotQrcode and getUpdates parse payloads and classify -14', async () => {
    const calls = [];
    const mockFetch = async (url, init) => {
      calls.push({ url, init });
      if (url.includes('get_bot_qrcode')) {
        return {
          ok: true,
          status: 200,
          text: async () => JSON.stringify({ qrcode: 'qr1', qrcode_img_content: 'https://qr/1' }),
        };
      }
      if (url.includes('getupdates')) {
        return {
          ok: true,
          status: 200,
          text: async () => JSON.stringify({ ret: -14, errmsg: 'session expired' }),
        };
      }
      return { ok: true, status: 200, text: async () => '{}' };
    };

    const client = createIlinkClient({ fetchImpl: mockFetch, baseUrl: 'https://example.test' });
    const qr = await client.getBotQrcode();
    assert.strictEqual(qr.qrcode, 'qr1');
    assert.strictEqual(qr.qrcode_img_content, 'https://qr/1');
    assert.strictEqual(calls[0].url, 'https://example.test/ilink/bot/get_bot_qrcode?bot_type=3');

    await assert.rejects(
      () => client.getUpdates({ account: { bot_token: 'tok' }, cursor: 'cur1' }),
      (err) => err instanceof IlinkSessionExpiredError && err.code === -14
    );
  });

  test('sendText sends context_token and classifies timeout as IlinkTimeoutError', async () => {
    let sentBody = null;
    const client = createIlinkClient({
      fetchImpl: async (url, init) => {
        if (url.includes('sendmessage')) {
          sentBody = JSON.parse(init.body);
          return { ok: true, status: 200, text: async () => JSON.stringify({ ret: 0 }) };
        }
        const err = new Error('The operation timed out');
        err.name = 'TimeoutError';
        throw err;
      },
    });

    await client.sendText({
      account: { bot_token: 't1', baseurl: 'https://bot.test' },
      toUserId: 'wx_1',
      contextToken: 'ctx_9',
      text: 'hello',
    });
    assert.strictEqual(sentBody.msg.to_user_id, 'wx_1');
    assert.strictEqual(sentBody.msg.context_token, 'ctx_9');
    assert.strictEqual(sentBody.msg.item_list[0].text_item.text, 'hello');

    await assert.rejects(
      () => client.getQrcodeStatus({ qrcode: 'q1' }),
      (err) => err instanceof IlinkTimeoutError
    );
  });

  test('sendText classifies ret=-2 as IlinkContextInvalidError, errcode=-14 as IlinkSessionExpiredError, and other errors cleanly', async () => {
    const responses = [
      { ret: -2, errmsg: 'invalid context' },
      { errcode: -14, errmsg: 'session expired' },
      { ret: 5001, errmsg: 'rate limited' },
    ];
    let idx = 0;
    const client = createIlinkClient({
      fetchImpl: async () => ({
        ok: true,
        status: 200,
        text: async () => JSON.stringify(responses[idx++]),
      }),
    });
    const baseArgs = {
      account: { bot_token: 't1', baseurl: 'https://bot.test' },
      toUserId: 'wx_1',
      contextToken: 'ctx_9',
      text: 'hello',
    };

    await assert.rejects(
      () => client.sendText(baseArgs),
      (err) => err instanceof IlinkContextInvalidError && err.code === -2
    );
    await assert.rejects(
      () => client.sendText(baseArgs),
      (err) => err instanceof IlinkSessionExpiredError && err.code === -14
    );
    await assert.rejects(
      () => client.sendText(baseArgs),
      (err) => err.message === 'iLink error 5001: rate limited'
    );
  });

  test('classifyPayloadError handles null/ok payloads, context-token regex on ret=-1, and non-string errmsg', () => {
    assert.strictEqual(classifyPayloadError(null), null);
    assert.strictEqual(classifyPayloadError({ ret: 0 }), null);
    assert.strictEqual(classifyPayloadError({ ret: Number.NaN, errcode: Infinity }), null);

    const ctxErr = classifyPayloadError({ ret: -1, errmsg: 'Context_Token expired' });
    assert.ok(ctxErr instanceof IlinkContextInvalidError);
    assert.strictEqual(ctxErr.code, -2);

    const objMsgErr = classifyPayloadError({ ret: 4002, errmsg: { nested: true } });
    assert.strictEqual(objMsgErr.code, 4002);
    assert.strictEqual(objMsgErr.message, 'iLink error 4002: request failed');
  });
});
