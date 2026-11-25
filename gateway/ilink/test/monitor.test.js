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
      assert.strictEqual(sent.length, 2);
      assert.strictEqual(sent[0].contextToken, 'ctx_abc');
      assert.strictEqual(sent[1].contextToken, 'ctx_def');
      assert.strictEqual(statusUpdates[0].status, 'session_expired');
    } finally {
      fs.rmSync(tmpRoot, { recursive: true, force: true });
    }
  });

  test('splitReplyChunks splits long messages cleanly', () => {
    assert.deepStrictEqual(splitReplyChunks('abcdef', 2), ['ab', 'cd', 'ef']);
  });
});
