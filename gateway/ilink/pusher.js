'use strict';

const { GatewayHttpError } = require('./api');
const { IlinkSessionExpiredError, IlinkContextInvalidError } = require('./client');
const { formatPushText, splitPushChunks } = require('./formatter');

function normalizePositiveInt(value, fallback) {
  const n = Number(value);
  if (!Number.isFinite(n) || n <= 0) return fallback;
  return Math.floor(n);
}

function normalizeNonNegativeInt(value, fallback) {
  const n = Number(value);
  if (!Number.isFinite(n) || n < 0) return fallback;
  return Math.floor(n);
}

function hasValidContext(ctx, account) {
  if (!ctx || !ctx.context_token || ctx.invalidated) {
    return false;
  }
  const hasOwner = Boolean(
    account && account.ilink_user_id ? ctx.from_user_id === account.ilink_user_id : ctx.from_user_id
  );
  if (!hasOwner) {
    return false;
  }
  const toUserId = (account && account.ilink_user_id) || ctx.from_user_id;
  return Boolean(toUserId);
}

function createPusher({
  client,
  state,
  account,
  instanceId,
  api,
  log = console,
  signal,
  pollMs = process.env.ILINK_PUSH_POLL_MS,
  batchLimit = process.env.ILINK_PUSH_BATCH_LIMIT,
  sendGapMs = process.env.ILINK_PUSH_SEND_GAP_MS,
  replyChunk = process.env.ILINK_REPLY_CHUNK,
  sleepImpl,
} = {}) {
  const safePollMs = normalizePositiveInt(pollMs === undefined ? 3000 : pollMs, 3000);
  const safeBatchLimit = normalizePositiveInt(batchLimit === undefined ? 5 : batchLimit, 5);
  const safeSendGapMs = normalizeNonNegativeInt(sendGapMs === undefined ? 500 : sendGapMs, 500);
  const safeReplyChunk = normalizePositiveInt(replyChunk === undefined ? 1800 : replyChunk, 1800);

  let busy = false;
  let pendingWake = false;
  let wakeSleep = null;

  function defaultAbortSleep(ms) {
    if (!Number.isFinite(ms) || ms <= 0 || (signal && signal.aborted)) {
      return Promise.resolve();
    }
    return new Promise((resolve) => {
      let done = false;
      const finish = () => {
        if (done) return;
        done = true;
        clearTimeout(timer);
        if (signal) signal.removeEventListener('abort', finish);
        resolve();
      };
      const timer = setTimeout(finish, ms);
      if (signal) signal.addEventListener('abort', finish, { once: true });
    });
  }

  function defaultWakeSleep(ms) {
    if (!Number.isFinite(ms) || ms <= 0 || pendingWake || (signal && signal.aborted)) {
      return Promise.resolve();
    }
    return new Promise((resolve) => {
      let done = false;
      const finish = () => {
        if (done) return;
        done = true;
        clearTimeout(timer);
        if (signal) signal.removeEventListener('abort', finish);
        if (wakeSleep === finish) wakeSleep = null;
        resolve();
      };
      wakeSleep = finish;
      const timer = setTimeout(finish, ms);
      if (signal) signal.addEventListener('abort', finish, { once: true });
    });
  }

  const waitGap = sleepImpl
    ? (ms) => {
        if (!Number.isFinite(ms) || ms <= 0 || (signal && signal.aborted)) return Promise.resolve();
        return sleepImpl(ms);
      }
    : defaultAbortSleep;

  const waitPoll = sleepImpl
    ? (ms) => {
        if (!Number.isFinite(ms) || ms <= 0 || pendingWake || (signal && signal.aborted)) {
          return Promise.resolve();
        }
        return sleepImpl(ms);
      }
    : defaultWakeSleep;

  async function sendChunkWithOnceRetry(toUserId, contextToken, text) {
    try {
      await client.sendText({ account, toUserId, contextToken, text, signal });
    } catch (err) {
      if (err instanceof IlinkSessionExpiredError || err instanceof IlinkContextInvalidError) {
        throw err;
      }
      if (signal && signal.aborted) {
        throw err;
      }
      await client.sendText({ account, toUserId, contextToken, text, signal });
    }
  }

  async function ackSentWithOnceRetry(pushId) {
    try {
      await api.ackPush(instanceId, pushId, { result: 'sent' });
    } catch (err) {
      if (signal && signal.aborted) {
        log.warn?.('[ilink-pusher] ack sent failed:', err.message);
        return;
      }
      try {
        await api.ackPush(instanceId, pushId, { result: 'sent' });
      } catch (retryErr) {
        log.warn?.('[ilink-pusher] ack sent failed:', retryErr.message);
      }
    }
  }

  async function processOne(item) {
    const ctx = state.readContext();
    if (!hasValidContext(ctx, account)) {
      await api
        .ackPush(instanceId, item.id, {
          result: 'waiting_context',
          error: '请先在微信向机器人发送任意一条消息以建立会话',
        })
        .catch((ackErr) => {
          log.warn?.('[ilink-pusher] ack waiting_context failed:', ackErr.message);
        });
      return;
    }

    const toUserId = (account && account.ilink_user_id) || ctx.from_user_id;
    if (!toUserId) {
      await api
        .ackPush(instanceId, item.id, {
          result: 'waiting_context',
          error: '请先在微信向机器人发送任意一条消息以建立会话',
        })
        .catch((ackErr) => {
          log.warn?.('[ilink-pusher] ack waiting_context failed:', ackErr.message);
        });
      return;
    }

    const formatted = formatPushText(item.title, item.content);
    const chunks = splitPushChunks(formatted, safeReplyChunk);
    if (chunks.length === 0) {
      await ackSentWithOnceRetry(item.id);
      return;
    }

    let delivered = false;
    try {
      for (let i = 0; i < chunks.length; i += 1) {
        if (signal && signal.aborted) return;
        await sendChunkWithOnceRetry(toUserId, ctx.context_token, chunks[i]);
        if (i + 1 < chunks.length && safeSendGapMs > 0) {
          await waitGap(safeSendGapMs);
        }
      }
      delivered = true;
    } catch (err) {
      if (err instanceof IlinkSessionExpiredError) {
        await api
          .updateStatus(instanceId, {
            status: 'session_expired',
            last_error: '微信登录态已过期，请重新扫码登录',
          })
          .catch(() => {});
        await api
          .ackPush(instanceId, item.id, {
            result: item.kind === 'test' ? 'failed' : 'retry',
            error: '微信登录态已过期，请重新扫码登录',
          })
          .catch(() => {});
        throw err;
      }
      if (err instanceof IlinkContextInvalidError) {
        const latest = state.readContext();
        if (!latest || latest.context_token === ctx.context_token) {
          state.writeContext({ ...(latest || ctx), invalidated: true });
        } else if (!latest.invalidated) {
          pendingWake = true;
        }
        await api
          .ackPush(instanceId, item.id, {
            result: 'waiting_context',
            error: '微信会话上下文已过期，请在微信向机器人发送任意消息后重试',
          })
          .catch((ackErr) => {
            log.warn?.('[ilink-pusher] ack waiting_context failed:', ackErr.message);
          });
        return;
      }
      if (signal && signal.aborted) {
        return;
      }
      log.warn?.('[ilink-pusher] send failed:', err.message);
      await api
        .ackPush(instanceId, item.id, {
          result: 'retry',
          error: err.message || '发送失败',
        })
        .catch(() => {});
      return;
    }

    if (delivered) {
      await ackSentWithOnceRetry(item.id);
    }
  }

  async function pumpOnce() {
    if (busy) {
      pendingWake = true;
      return;
    }
    busy = true;
    try {
      do {
        pendingWake = false;
        const ctx = state.readContext();
        const valid = hasValidContext(ctx, account);
        const claimed = await api.claimPushes(instanceId, {
          has_context: valid,
          context_updated_at: (ctx && ctx.updated_at) || '',
          limit: safeBatchLimit,
        });
        const items = Array.isArray(claimed && claimed.items) ? claimed.items : [];
        for (let i = 0; i < items.length; i += 1) {
          if (signal && signal.aborted) break;
          await processOne(items[i]);
          if (i + 1 < items.length && safeSendGapMs > 0) {
            await waitGap(safeSendGapMs);
          }
        }
      } while (pendingWake && (!signal || !signal.aborted));
    } finally {
      busy = false;
    }
  }

  function wake() {
    pendingWake = true;
    if (wakeSleep) {
      wakeSleep();
    }
  }

  async function run() {
    while (!signal || !signal.aborted) {
      try {
        await pumpOnce();
      } catch (err) {
        if (err instanceof IlinkSessionExpiredError) {
          throw err;
        }
        if (err instanceof GatewayHttpError && (err.code === 1003 || err.code === 1004)) {
          break;
        }
        if (signal && signal.aborted) break;
        log.warn?.('[ilink-pusher] claim cycle failed:', err.message);
      }
      if (signal && signal.aborted) break;
      if (!pendingWake) {
        await waitPoll(safePollMs);
      }
    }
  }

  return {
    pumpOnce,
    wake,
    run,
  };
}

module.exports = {
  hasValidContext,
  createPusher,
};
