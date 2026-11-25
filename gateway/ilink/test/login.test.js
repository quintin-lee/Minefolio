'use strict';

const { test, describe } = require('node:test');
const assert = require('node:assert');
const fs = require('fs');
const os = require('os');
const path = require('path');

const { createStateStore } = require('../state');
const { runLogin } = require('../login');

describe('ilink/login', () => {
  test('handles wait -> scaned -> need_verifycode -> scaned -> confirmed', async () => {
    const tmpRoot = fs.mkdtempSync(path.join(os.tmpdir(), 'mf-ilink-login-'));
    try {
      const state = createStateStore({ rootDir: tmpRoot, instanceId: 1 });
      const statuses = [];
      const seenVerify = [];
      const seq = [
        { status: 'wait' },
        { status: 'scaned' },
        { status: 'need_verifycode' },
        { status: 'scaned' },
        {
          status: 'confirmed',
          bot_token: 'bot_tok_1',
          ilink_bot_id: 'bot_1@im.bot',
          ilink_user_id: 'wx_owner_1',
          baseurl: 'https://redirected.test',
        },
      ];
      let pinCalls = 0;

      const client = {
        getBotQrcode: async () => ({ qrcode: 'q1', qrcode_img_content: 'https://qr.test/1' }),
        getQrcodeStatus: async ({ verifyCode }) => {
          seenVerify.push(verifyCode || null);
          return seq.shift();
        },
      };

      const res = await runLogin({
        client,
        state,
        onStatus: async (p) => statuses.push(p),
        fetchVerifyCode: async () => {
          pinCalls += 1;
          return pinCalls === 1 ? '654321' : null;
        },
        shortPollMs: 10,
        longPollMs: 10,
      });

      assert.strictEqual(res.ok, true);
      assert.strictEqual(res.account.ilink_bot_id, 'bot_1@im.bot');
      assert.strictEqual(state.readAccount().bot_token, 'bot_tok_1');
      assert.deepStrictEqual(
        statuses.map((s) => s.status),
        ['qr_ready', 'scanned', 'need_verifycode', 'scanned', 'active']
      );
      assert.strictEqual(statuses[0].qr_content, 'https://qr.test/1');
      assert.ok(seenVerify.includes('654321'));
    } finally {
      fs.rmSync(tmpRoot, { recursive: true, force: true });
    }
  });

  test('handles scaned_but_redirect and expired max 3 refreshes', async () => {
    const tmpRoot = fs.mkdtempSync(path.join(os.tmpdir(), 'mf-ilink-login2-'));
    try {
      const state = createStateStore({ rootDir: tmpRoot, instanceId: 2 });
      const usedBaseUrls = [];
      let qrCalls = 0;

      const clientRedirect = {
        getBotQrcode: async () => ({ qrcode: 'qr_r', qrcode_img_content: 'raw_qr' }),
        getQrcodeStatus: async ({ baseUrl }) => {
          usedBaseUrls.push(baseUrl || 'default');
          if (usedBaseUrls.length === 1) {
            return { status: 'scaned_but_redirect', redirect_host: 'hk.ilink.test' };
          }
          return {
            status: 'confirmed',
            bot_token: 't_hk',
            ilink_bot_id: 'b_hk',
            ilink_user_id: 'u_hk',
          };
        },
      };

      const res1 = await runLogin({ client: clientRedirect, state });
      assert.strictEqual(res1.ok, true);
      assert.deepStrictEqual(usedBaseUrls, ['default', 'https://hk.ilink.test']);

      const expiredStatuses = [];
      const clientExpired = {
        getBotQrcode: async () => {
          qrCalls += 1;
          return { qrcode: `q_${qrCalls}`, qrcode_img_content: `url_${qrCalls}` };
        },
        getQrcodeStatus: async () => ({ status: 'expired' }),
      };

      const res2 = await runLogin({
        client: clientExpired,
        state,
        onStatus: async (p) => expiredStatuses.push(p.status),
        maxRefresh: 3,
      });
      assert.strictEqual(res2.ok, false);
      assert.strictEqual(res2.reason, 'expired');
      assert.strictEqual(qrCalls, 4); // initial + 3 refreshes
      assert.strictEqual(expiredStatuses[expiredStatuses.length - 1], 'expired');
    } finally {
      fs.rmSync(tmpRoot, { recursive: true, force: true });
    }
  });

  test('returns already_bound on binded_redirect', async () => {
    const tmpRoot = fs.mkdtempSync(path.join(os.tmpdir(), 'mf-ilink-login3-'));
    try {
      const state = createStateStore({ rootDir: tmpRoot, instanceId: 3 });
      const statuses = [];
      const res = await runLogin({
        client: {
          getBotQrcode: async () => ({ qrcode: 'q', qrcode_img_content: 'c' }),
          getQrcodeStatus: async () => ({ status: 'binded_redirect' }),
        },
        state,
        onStatus: async (p) => statuses.push(p),
      });
      assert.strictEqual(res.ok, false);
      assert.strictEqual(res.reason, 'already_bound');
      assert.strictEqual(statuses[statuses.length - 1].status, 'error');
    } finally {
      fs.rmSync(tmpRoot, { recursive: true, force: true });
    }
  });
});
