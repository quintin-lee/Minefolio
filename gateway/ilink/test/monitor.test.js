'use strict';

const { test, describe } = require('node:test');
const assert = require('node:assert');
const fs = require('fs');
const os = require('os');
const path = require('path');

const { createStateStore } = require('../state');
const { IlinkSessionExpiredError } = require('../client');
const { runMonitor, splitReplyChunks } = require('../monitor');

describe('ilink/monitor', () => {
  test('filters self/group/stranger/duplicate, preserves context_token, persists cursor, handles -14', async () => {
    const tmpRoot = fs.mkdtempSync(path.join(os.tmpdir(), 'mf-ilink-mon-'));
    try {
      const state = createStateStore({ rootDir: tmpRoot, instanceId: 1 });
      const ac = new AbortController();
      const sent = [];
      const chatted = [];
      const statusUpdates = [];
      const ctxUpdates = [];
      let pollBatch = 0;

      const client = {
        getUpdates: async () => {
          pollBatch += 1;
          if (pollBatch === 1) {
            return {
              get_updates_buf: 'cursor_v1',
              msgs: [
                {
                  message_id: 'm_self',
                  message_type: 2,
                  from_user_id: 'owner_wx',
                  item_list: [{ type: 1, text_item: { text: 'self' } }],
                },
                {
                  message_id: 'm_grp',
                  message_type: 1,
                  group_id: 'grp_1',
                  from_user_id: 'owner_wx',
                  item_list: [{ type: 1, text_item: { text: 'group' } }],
                },
                {
                  message_id: 'm_stranger',
                  message_type: 1,
                  from_user_id: 'stranger_wx',
                  item_list: [{ type: 1, text_item: { text: 'hi' } }],
                },
                {
                  message_id: 'm_ok_1',
                  message_type: 1,
                  from_user_id: 'owner_wx',
                  context_token: 'ctx_abc',
                  item_list: [{ type: 1, text_item: { text: '查总资产' } }],
                },
                {
                  message_id: 'm_ok_1',
                  message_type: 1,
                  from_user_id: 'owner_wx',
                  context_token: 'ctx_abc',
                  item_list: [{ type: 1, text_item: { text: '查总资产' } }],
                },
                {
                  message_id: 'm_voice_2',
                  message_type: 1,
                  from_user_id: 'owner_wx',
                  context_token: 'ctx_def',
                  item_list: [{ type: 3, voice_item: { text: '本月支出' } }],
                },
                {
                  message_id: 'm_img_3',
                  message_type: 1,
                  from_user_id: 'owner_wx',
                  context_token: 'ctx_def',
                  item_list: [{ type: 2 }],
                },
              ],
            };
          }
          // Wait briefly for queue to drain, then trigger -14
          await new Promise((r) => setTimeout(r, 40));
          throw new IlinkSessionExpiredError('expired');
        },
        sendText: async (args) => {
          sent.push(args);
        },
      };

      const api = {
        chat: async (instId, imUserId, text) => {
          chatted.push({ instId, imUserId, text });
          return { task_id: chatted.length };
        },
        pollTask: async (_instId, taskId) => ({ reply: `reply_${taskId}` }),
        updateStatus: async (_instId, payload) => {
          statusUpdates.push(payload);
        },
      };

      await assert.rejects(
        () =>
          runMonitor({
            client,
            state,
            account: { bot_token: 'tok', ilink_user_id: 'owner_wx' },
            instanceId: 1,
            api,
            signal: ac.signal,
            pollTaskOpts: { intervalMs: 5, timeoutMs: 1000 },
            onContextUpdated: (ev) => ctxUpdates.push(ev),
          }),
        (err) => err instanceof IlinkSessionExpiredError
      );

      assert.strictEqual(state.readCursor().get_updates_buf, 'cursor_v1');
      assert.strictEqual(chatted.length, 2);
      assert.deepStrictEqual(
        chatted.map((c) => [c.imUserId, c.text]),
        [
          ['ilink:owner_wx', '查总资产'],
          ['ilink:owner_wx', '本月支出'],
        ]
      );
      const replies = sent.filter((s) => String(s.text).startsWith('reply_'));
      assert.strictEqual(replies.length, 2);
      assert.strictEqual(replies[0].contextToken, 'ctx_abc');
      assert.strictEqual(replies[1].contextToken, 'ctx_def');
      assert.ok(sent.some((s) => s.text === '目前仅支持文字消息' && s.contextToken === 'ctx_def'));
      assert.strictEqual(statusUpdates[0].status, 'session_expired');
      assert.strictEqual(state.readContext().context_token, 'ctx_def');
      assert.strictEqual(state.readContext().invalidated, false);
      assert.ok(ctxUpdates.some((e) => e.contextToken === 'ctx_def'));
    } finally {
      fs.rmSync(tmpRoot, { recursive: true, force: true });
    }
  });

  test('ignores context_token for self/group/stranger/duplicate and updates on owner media', async () => {
    const tmpRoot = fs.mkdtempSync(path.join(os.tmpdir(), 'mf-ilink-mon-ctx-'));
    try {
      const state = createStateStore({ rootDir: tmpRoot, instanceId: 1 });
      const ac = new AbortController();
      const ctxUpdates = [];
      let pollBatch = 0;

      const client = {
        getUpdates: async () => {
          pollBatch += 1;
          if (pollBatch === 1) {
            return {
              msgs: [
                {
                  message_id: 'm_self_ctx',
                  message_type: 2,
                  from_user_id: 'owner_wx',
                  context_token: 'ctx_self',
                  item_list: [{ type: 1, text_item: { text: 'self' } }],
                },
                {
                  message_id: 'm_grp_ctx',
                  message_type: 1,
                  group_id: 'grp_1',
                  from_user_id: 'owner_wx',
                  context_token: 'ctx_grp',
                  item_list: [{ type: 1, text_item: { text: 'group' } }],
                },
                {
                  message_id: 'm_stranger_ctx',
                  message_type: 1,
                  from_user_id: 'stranger_wx',
                  context_token: 'ctx_stranger',
                  item_list: [{ type: 1, text_item: { text: 'hi' } }],
                },
              ],
            };
          }
          if (pollBatch === 2) {
            assert.strictEqual(state.readContext(), null);
            assert.strictEqual(ctxUpdates.length, 0);
            return {
              msgs: [
                {
                  message_id: 'm_img_owner',
                  message_type: 1,
                  from_user_id: 'owner_wx',
                  context_token: 'ctx_img',
                  item_list: [{ type: 2 }],
                },
                {
                  message_id: 'm_img_owner',
                  message_type: 1,
                  from_user_id: 'owner_wx',
                  context_token: 'ctx_dup_ignored',
                  item_list: [{ type: 2 }],
                },
              ],
            };
          }
          throw new IlinkSessionExpiredError('expired');
        },
        sendText: async () => {},
      };

      const api = {
        chat: async () => {
          throw new Error('should not chat for unsupported media');
        },
        pollTask: async () => ({}),
        updateStatus: async () => {},
      };

      await assert.rejects(
        () =>
          runMonitor({
            client,
            state,
            account: { bot_token: 'tok', ilink_user_id: 'owner_wx' },
            instanceId: 1,
            api,
            signal: ac.signal,
            onContextUpdated: (ev) => ctxUpdates.push(ev),
          }),
        (err) => err instanceof IlinkSessionExpiredError
      );

      assert.strictEqual(state.readContext().context_token, 'ctx_img');
      assert.strictEqual(state.readContext().from_user_id, 'owner_wx');
      assert.strictEqual(state.readContext().invalidated, false);
      assert.strictEqual(ctxUpdates.length, 1);
      assert.strictEqual(ctxUpdates[0].contextToken, 'ctx_img');
    } finally {
      fs.rmSync(tmpRoot, { recursive: true, force: true });
    }
  });

  test('splitReplyChunks splits long messages cleanly', () => {
    assert.deepStrictEqual(splitReplyChunks('abcdef', 2), ['ab', 'cd', 'ef']);
  });
});
