'use strict';

const { test, describe } = require('node:test');
const assert = require('node:assert');
const fs = require('fs');
const os = require('os');
const path = require('path');

const { createStateStore } = require('../state');
const { GatewayHttpError } = require('../api');
const { IlinkSessionExpiredError, IlinkContextInvalidError } = require('../client');
const { createPusher, hasValidContext } = require('../pusher');
const { parseArgs, runManagedSession } = require('../runner');

const silentLog = { warn: () => {}, error: () => {} };

function makeTmpDir() {
  return fs.mkdtempSync(path.join(os.tmpdir(), 'mf-ilink-push-'));
}

describe('ilink/pusher', () => {
  test('claims, formats, splits, sends chunks, and acks sent', async () => {
    const tmpRoot = makeTmpDir();
    try {
      const state = createStateStore({ rootDir: tmpRoot, instanceId: 1 });
      state.writeContext({
        from_user_id: 'owner_wx',
        context_token: 'ctx_1',
        updated_at: '2026-11-25T10:00:00Z',
        invalidated: false,
      });

      const claimedPayloads = [];
      const acked = [];
      const sent = [];

      const pusher = createPusher({
        state,
        account: { bot_token: 'tok', ilink_user_id: 'owner_wx' },
        instanceId: 1,
        replyChunk: 12,
        sendGapMs: 0,
        client: {
          sendText: async (args) => {
            sent.push(args);
          },
        },
        api: {
          claimPushes: async (_id, payload) => {
            claimedPayloads.push(payload);
            return { items: [{ id: 11, kind: 'scheduler', title: '早报', content: 'A'.repeat(20) }] };
          },
          ackPush: async (_id, pushId, payload) => {
            acked.push({ pushId, payload });
          },
          updateStatus: async () => {},
        },
      });

      await pusher.pumpOnce();
      assert.strictEqual(claimedPayloads[0].has_context, true);
      assert.strictEqual(claimedPayloads[0].context_updated_at, '2026-11-25T10:00:00Z');
      assert.ok(sent.length >= 2);
      assert.strictEqual(sent[0].contextToken, 'ctx_1');
      assert.deepStrictEqual(acked, [{ pushId: 11, payload: { result: 'sent' } }]);
    } finally {
      fs.rmSync(tmpRoot, { recursive: true, force: true });
    }
  });

  test('has_context=false and invalid context_token (-2) ack waiting_context and invalidate local context', async () => {
    const tmpRoot = makeTmpDir();
    try {
      const state = createStateStore({ rootDir: tmpRoot, instanceId: 1 });
      const acks = [];
      const claims = [];
      let call = 0;

      const pusher = createPusher({
        state,
        account: { bot_token: 'tok', ilink_user_id: 'owner_wx' },
        instanceId: 1,
        sendGapMs: 0,
        log: silentLog,
        client: {
          sendText: async () => {
            throw new IlinkContextInvalidError('expired ctx');
          },
        },
        api: {
          claimPushes: async (_id, payload) => {
            claims.push(payload);
            call += 1;
            if (call === 1) {
              return { items: [{ id: 21, kind: 'test', title: '测试', content: 'hi' }] };
            }
            return { items: [{ id: 22, kind: 'scheduler', title: '日报', content: 'body' }] };
          },
          ackPush: async (_id, pushId, payload) => {
            acks.push({ pushId, result: payload.result });
          },
          updateStatus: async () => {},
        },
      });

      // Missing context -> claims with has_context=false, processOne acks waiting_context
      await pusher.pumpOnce();
      assert.strictEqual(claims[0].has_context, false);
      assert.strictEqual(acks[0].pushId, 21);
      assert.strictEqual(acks[0].result, 'waiting_context');

      // With context -> client throws -2 -> context.invalidated becomes true & acks waiting_context
      state.writeContext({
        from_user_id: 'owner_wx',
        context_token: 'ctx_bad',
        updated_at: '2026-11-25T10:05:00Z',
        invalidated: false,
      });
      await pusher.pumpOnce();
      assert.strictEqual(state.readContext().invalidated, true);
      assert.strictEqual(acks[1].pushId, 22);
      assert.strictEqual(acks[1].result, 'waiting_context');
    } finally {
      fs.rmSync(tmpRoot, { recursive: true, force: true });
    }
  });

  test('wake() triggers immediate re-claim without waiting full pollMs and clamps invalid pollMs', async () => {
    const tmpRoot = makeTmpDir();
    try {
      const state = createStateStore({ rootDir: tmpRoot, instanceId: 1 });
      const ac = new AbortController();
      let claimCount = 0;

      const pusher = createPusher({
        state,
        account: { bot_token: 'tok', ilink_user_id: 'owner_wx' },
        instanceId: 1,
        pollMs: 0, // non-positive clamps to 3000ms instead of tight-spinning
        sendGapMs: 0,
        signal: ac.signal,
        client: { sendText: async () => {} },
        api: {
          claimPushes: async () => {
            claimCount += 1;
            if (claimCount >= 2) ac.abort();
            return { items: [] };
          },
          ackPush: async () => {},
          updateStatus: async () => {},
        },
      });

      const running = pusher.run();
      await new Promise((r) => setTimeout(r, 25));
      assert.strictEqual(claimCount, 1);
      state.writeContext({
        from_user_id: 'owner_wx',
        context_token: 'ctx_woke',
        updated_at: '2026-11-25T10:10:00Z',
        invalidated: false,
      });
      pusher.wake();
      await running;
      assert.ok(claimCount >= 2);
    } finally {
      fs.rmSync(tmpRoot, { recursive: true, force: true });
    }
  });

  test('wake() during sendGapMs does NOT shorten gap delay while coalescing follow-up claimPushes', async () => {
    const tmpRoot = makeTmpDir();
    try {
      const state = createStateStore({ rootDir: tmpRoot, instanceId: 1 });
      state.writeContext({
        from_user_id: 'owner_wx',
        context_token: 'ctx_1',
        updated_at: '2026-11-25T10:00:00Z',
        invalidated: false,
      });

      const sendTimes = [];
      let claimCount = 0;

      const pusher = createPusher({
        state,
        account: { bot_token: 'tok', ilink_user_id: 'owner_wx' },
        instanceId: 1,
        replyChunk: 15,
        sendGapMs: 45,
        client: {
          sendText: async () => {
            sendTimes.push(Date.now());
            if (sendTimes.length === 1) {
              setTimeout(() => pusher.wake(), 5);
            }
          },
        },
        api: {
          claimPushes: async () => {
            claimCount += 1;
            if (claimCount === 1) {
              return {
                items: [{ id: 61, kind: 'scheduler', title: 'T', content: 'FIRSTCHUNK\n\nSECONDCHUN' }],
              };
            }
            return { items: [] };
          },
          ackPush: async () => {},
          updateStatus: async () => {},
        },
      });

      await pusher.pumpOnce();
      assert.strictEqual(sendTimes.length, 2);
      assert.ok(sendTimes[1] - sendTimes[0] >= 35, `expected gap >= 35ms, got ${sendTimes[1] - sendTimes[0]}`);
      assert.strictEqual(claimCount, 2);
    } finally {
      fs.rmSync(tmpRoot, { recursive: true, force: true });
    }
  });

  test('sendText throwing IlinkSessionExpiredError (-14) updates status, acks retry, and throws', async () => {
    const tmpRoot = makeTmpDir();
    try {
      const state = createStateStore({ rootDir: tmpRoot, instanceId: 1 });
      state.writeContext({
        from_user_id: 'owner_wx',
        context_token: 'ctx_1',
        updated_at: '2026-11-25T10:00:00Z',
        invalidated: false,
      });
      const updates = [];
      const acks = [];

      const pusher = createPusher({
        state,
        account: { bot_token: 'tok', ilink_user_id: 'owner_wx' },
        instanceId: 1,
        sendGapMs: 0,
        log: silentLog,
        client: {
          sendText: async () => {
            throw new IlinkSessionExpiredError('expired');
          },
        },
        api: {
          claimPushes: async () => ({
            items: [{ id: 31, kind: 'scheduler', title: 'T', content: 'C' }],
          }),
          ackPush: async (_id, pushId, payload) => {
            acks.push({ pushId, result: payload.result });
          },
          updateStatus: async (_id, payload) => {
            updates.push(payload.status);
          },
        },
      });

      await assert.rejects(() => pusher.run(), (err) => err instanceof IlinkSessionExpiredError);
      assert.deepStrictEqual(updates, ['session_expired']);
      assert.deepStrictEqual(acks, [{ pushId: 31, result: 'retry' }]);
    } finally {
      fs.rmSync(tmpRoot, { recursive: true, force: true });
    }
  });

  test('transient error retries chunk 2 in-place before acking retry after double failure', async () => {
    const tmpRoot = makeTmpDir();
    try {
      const state = createStateStore({ rootDir: tmpRoot, instanceId: 1 });
      state.writeContext({
        from_user_id: 'owner_wx',
        context_token: 'ctx_1',
        updated_at: '2026-11-25T10:00:00Z',
        invalidated: false,
      });

      const sentTexts = [];
      const acks = [];

      const pusher = createPusher({
        state,
        account: { bot_token: 'tok', ilink_user_id: 'owner_wx' },
        instanceId: 1,
        replyChunk: 15,
        sendGapMs: 0,
        log: silentLog,
        client: {
          sendText: async ({ text }) => {
            sentTexts.push(text);
            if (text.includes('SECOND')) {
              throw new Error('network timeout');
            }
          },
        },
        api: {
          claimPushes: async () => ({
            items: [
              {
                id: 41,
                kind: 'scheduler',
                title: 'T',
                content: 'FIRSTCHUNK\n\nSECONDCHUN',
              },
            ],
          }),
          ackPush: async (_id, pushId, payload) => {
            acks.push({ pushId, payload });
          },
          updateStatus: async () => {},
        },
      });

      await pusher.pumpOnce();
      assert.strictEqual(sentTexts.filter((t) => t.includes('SECOND')).length, 2);
      assert.strictEqual(sentTexts.filter((t) => !t.includes('SECOND')).length, 1);
      assert.strictEqual(acks[0].pushId, 41);
      assert.strictEqual(acks[0].payload.result, 'retry');
    } finally {
      fs.rmSync(tmpRoot, { recursive: true, force: true });
    }
  });

  test('ackPush(sent) throwing after sendText succeeds retries sent once and never downgrades to retry', async () => {
    const tmpRoot = makeTmpDir();
    try {
      const state = createStateStore({ rootDir: tmpRoot, instanceId: 1 });
      state.writeContext({
        from_user_id: 'owner_wx',
        context_token: 'ctx_1',
        updated_at: '2026-11-25T10:00:00Z',
        invalidated: false,
      });

      const ackPayloads = [];
      const pusher = createPusher({
        state,
        account: { bot_token: 'tok', ilink_user_id: 'owner_wx' },
        instanceId: 1,
        sendGapMs: 0,
        log: silentLog,
        client: { sendText: async () => {} },
        api: {
          claimPushes: async () => ({
            items: [{ id: 71, kind: 'scheduler', title: 'T', content: 'Hello' }],
          }),
          ackPush: async (_id, _pushId, payload) => {
            ackPayloads.push(payload.result);
            throw new Error('ack timeout');
          },
          updateStatus: async () => {},
        },
      });

      await pusher.pumpOnce();
      assert.deepStrictEqual(ackPayloads, ['sent', 'sent']);
    } finally {
      fs.rmSync(tmpRoot, { recursive: true, force: true });
    }
  });

  test('-2 after concurrent state.writeContext with newer context_token preserves newer valid context', async () => {
    const tmpRoot = makeTmpDir();
    try {
      const state = createStateStore({ rootDir: tmpRoot, instanceId: 1 });
      state.writeContext({
        from_user_id: 'owner_wx',
        context_token: 'ctx_old',
        updated_at: '2026-11-25T10:00:00Z',
        invalidated: false,
      });

      let claimCount = 0;
      const acks = [];

      const pusher = createPusher({
        state,
        account: { bot_token: 'tok', ilink_user_id: 'owner_wx' },
        instanceId: 1,
        sendGapMs: 0,
        log: silentLog,
        client: {
          sendText: async ({ contextToken }) => {
            if (contextToken === 'ctx_old') {
              state.writeContext({
                from_user_id: 'owner_wx',
                context_token: 'ctx_new',
                updated_at: '2026-11-25T10:01:00Z',
                invalidated: false,
              });
              throw new IlinkContextInvalidError('stale ctx');
            }
          },
        },
        api: {
          claimPushes: async () => {
            claimCount += 1;
            if (claimCount === 1) {
              return { items: [{ id: 72, kind: 'scheduler', title: 'T', content: 'Body' }] };
            }
            return { items: [] };
          },
          ackPush: async (_id, pushId, payload) => {
            acks.push({ pushId, result: payload.result });
          },
          updateStatus: async () => {},
        },
      });

      await pusher.pumpOnce();
      assert.strictEqual(state.readContext().context_token, 'ctx_new');
      assert.strictEqual(state.readContext().invalidated, false);
      assert.strictEqual(claimCount, 2);
      assert.deepStrictEqual(acks, [{ pushId: 72, result: 'waiting_context' }]);
    } finally {
      fs.rmSync(tmpRoot, { recursive: true, force: true });
    }
  });

  test('signal.aborted during sendText returns cleanly without acking retry', async () => {
    const tmpRoot = makeTmpDir();
    try {
      const state = createStateStore({ rootDir: tmpRoot, instanceId: 1 });
      state.writeContext({
        from_user_id: 'owner_wx',
        context_token: 'ctx_1',
        updated_at: '2026-11-25T10:00:00Z',
        invalidated: false,
      });

      const ac = new AbortController();
      const acks = [];

      const pusher = createPusher({
        state,
        account: { bot_token: 'tok', ilink_user_id: 'owner_wx' },
        instanceId: 1,
        sendGapMs: 0,
        signal: ac.signal,
        log: silentLog,
        client: {
          sendText: async () => {
            ac.abort();
            const err = new Error('aborted');
            err.name = 'AbortError';
            throw err;
          },
        },
        api: {
          claimPushes: async () => ({
            items: [{ id: 73, kind: 'scheduler', title: 'T', content: 'Body' }],
          }),
          ackPush: async (_id, pushId, payload) => {
            acks.push({ pushId, result: payload.result });
          },
          updateStatus: async () => {},
        },
      });

      await pusher.pumpOnce();
      assert.strictEqual(acks.length, 0);
    } finally {
      fs.rmSync(tmpRoot, { recursive: true, force: true });
    }
  });

  test('hasValidContext requires owner match/non-empty user, test push acks failed on -14, run stops on 1003', async () => {
    assert.strictEqual(hasValidContext(null, { ilink_user_id: 'owner_wx' }), false);
    assert.strictEqual(
      hasValidContext({ context_token: 'c', invalidated: false }, { ilink_user_id: 'owner_wx' }),
      false
    );
    assert.strictEqual(hasValidContext({ context_token: 'c', invalidated: false }, {}), false);
    assert.strictEqual(
      hasValidContext({ context_token: 'c', from_user_id: 'other_wx', invalidated: false }, { ilink_user_id: 'owner_wx' }),
      false
    );
    assert.strictEqual(
      hasValidContext({ context_token: 'c', from_user_id: 'owner_wx', invalidated: false }, { ilink_user_id: 'owner_wx' }),
      true
    );

    const tmpRoot = makeTmpDir();
    try {
      const state = createStateStore({ rootDir: tmpRoot, instanceId: 1 });
      state.writeContext({
        from_user_id: 'owner_wx',
        context_token: 'ctx_1',
        updated_at: '2026-11-25T10:00:00Z',
        invalidated: false,
      });
      const acks = [];
      const pusher = createPusher({
        state,
        account: { bot_token: 'tok', ilink_user_id: 'owner_wx' },
        instanceId: 1,
        sendGapMs: 0,
        log: silentLog,
        client: {
          sendText: async () => {
            throw new IlinkSessionExpiredError('expired');
          },
        },
        api: {
          claimPushes: async () => ({
            items: [{ id: 51, kind: 'test', title: 'Test', content: 'Body' }],
          }),
          ackPush: async (_id, pushId, payload) => {
            acks.push({ pushId, result: payload.result });
          },
          updateStatus: async () => {},
        },
      });

      await assert.rejects(() => pusher.run(), (err) => err instanceof IlinkSessionExpiredError);
      assert.deepStrictEqual(acks, [{ pushId: 51, result: 'failed' }]);

      let goneCalls = 0;
      const gonePusher = createPusher({
        state,
        account: { bot_token: 'tok', ilink_user_id: 'owner_wx' },
        instanceId: 1,
        pollMs: 10,
        sendGapMs: 0,
        log: silentLog,
        client: { sendText: async () => {} },
        api: {
          claimPushes: async () => {
            goneCalls += 1;
            throw new GatewayHttpError('not found', { code: 1003, http: 404 });
          },
          ackPush: async () => {},
          updateStatus: async () => {},
        },
      });
      await gonePusher.run();
      assert.strictEqual(goneCalls, 1);
    } finally {
      fs.rmSync(tmpRoot, { recursive: true, force: true });
    }
  });
});

describe('ilink/runner coordinator', () => {
  test('preserves parseArgs and exits 1 on crash, 2 after sibling ackPush(sent) on -14, 0 on stopSignal', async () => {
    assert.deepStrictEqual(parseArgs(['--instance', '7', '--force-login']), {
      instanceId: '7',
      forceLogin: true,
    });

    const tmpRoot = makeTmpDir();
    try {
      const state = createStateStore({ rootDir: tmpRoot, instanceId: 1 });
      state.writeContext({
        from_user_id: 'owner_wx',
        context_token: 'ctx_1',
        updated_at: '2026-11-25T10:00:00Z',
        invalidated: false,
      });

      // 1. Unexpected crash in monitor -> exitCode 1 (NOT masked as 0)
      const crashRes = await runManagedSession({
        state,
        account: { bot_token: 'tok', ilink_user_id: 'owner_wx' },
        instanceId: 1,
        log: silentLog,
        graceMs: 200,
        pusherOpts: { pollMs: 60000, sendGapMs: 0 },
        client: {
          getUpdates: async () => {
            throw new Error('unexpected monitor failure');
          },
          sendText: async () => {},
        },
        monitorOpts: {
          sleepImpl: () => Promise.reject(new Error('unexpected monitor crash')),
        },
        api: {
          claimPushes: async () => ({ items: [] }),
          ackPush: async () => {},
          updateStatus: async () => {},
          chat: async () => ({}),
          pollTask: async () => ({}),
        },
      });
      assert.strictEqual(crashRes.exitCode, 1);

      // 2. -14 in monitor awaits sibling in-flight ackPush('sent') before returning exitCode 2
      let ackSentDone = false;
      const expiredRes = await runManagedSession({
        state,
        account: { bot_token: 'tok', ilink_user_id: 'owner_wx' },
        instanceId: 1,
        log: silentLog,
        graceMs: 1000,
        pusherOpts: { pollMs: 60000, sendGapMs: 0 },
        client: {
          getUpdates: async () => {
            await new Promise((r) => setTimeout(r, 15));
            throw new IlinkSessionExpiredError('expired');
          },
          sendText: async () => {},
        },
        api: {
          claimPushes: async () => ({
            items: [{ id: 81, kind: 'scheduler', title: 'T', content: 'Body' }],
          }),
          ackPush: async (_id, _pushId, payload) => {
            assert.strictEqual(payload.result, 'sent');
            await new Promise((r) => setTimeout(r, 40));
            ackSentDone = true;
          },
          updateStatus: async () => {},
          chat: async () => ({}),
          pollTask: async () => ({}),
        },
      });
      assert.strictEqual(expiredRes.exitCode, 2);
      assert.strictEqual(ackSentDone, true);

      // 3. External stopSignal aborts monitor's default 2000ms backoff sleep immediately -> exitCode 0
      const backoffStopAc = new AbortController();
      const tStart = Date.now();
      setTimeout(() => backoffStopAc.abort(), 15);
      const backoffStopRes = await runManagedSession({
        state,
        account: { bot_token: 'tok', ilink_user_id: 'owner_wx' },
        instanceId: 1,
        stopSignal: backoffStopAc.signal,
        graceMs: 1000,
        log: silentLog,
        pusherOpts: { pollMs: 60000, sendGapMs: 0 },
        client: {
          getUpdates: async () => {
            throw new Error('transient poll failure');
          },
          sendText: async () => {},
        },
        api: {
          claimPushes: async () => ({ items: [] }),
          ackPush: async () => {},
          updateStatus: async () => {},
          chat: async () => ({}),
          pollTask: async () => ({}),
        },
      });
      assert.strictEqual(backoffStopRes.exitCode, 0);
      assert.ok(Date.now() - tStart < 500, 'expected monitor backoff sleep to abort immediately');

      // 4. External stopSignal resolves cleanly with exitCode 0 even if a worker hangs past graceMs
      const stopAc = new AbortController();
      setTimeout(() => stopAc.abort(), 15);
      const stopRes = await runManagedSession({
        state,
        account: { bot_token: 'tok', ilink_user_id: 'owner_wx' },
        instanceId: 1,
        stopSignal: stopAc.signal,
        graceMs: 30,
        log: silentLog,
        pusherOpts: { pollMs: 60000, sendGapMs: 0 },
        client: {
          getUpdates: () => new Promise(() => {}),
          sendText: async () => {},
        },
        api: {
          claimPushes: async () => ({ items: [] }),
          ackPush: async () => {},
          updateStatus: async () => {},
          chat: async () => ({}),
          pollTask: async () => ({}),
        },
      });
      assert.strictEqual(stopRes.exitCode, 0);
    } finally {
      fs.rmSync(tmpRoot, { recursive: true, force: true });
    }
  });
});
