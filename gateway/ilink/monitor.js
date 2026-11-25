'use strict';

const { IlinkSessionExpiredError, IlinkTimeoutError } = require('./client');

function splitReplyChunks(text, chunkSize = Number(process.env.ILINK_REPLY_CHUNK || 1800)) {
  const s = String(text || '');
  if (!s) return [];
  const size = Math.max(1, Number(chunkSize) || 1800);
  const out = [];
  for (let i = 0; i < s.length; i += size) {
    out.push(s.slice(i, i + size));
  }
  return out;
}

function extractMessageText(msg) {
  const items = Array.isArray(msg && msg.item_list) ? msg.item_list : [];
  for (const item of items) {
    if (!item) continue;
    if (item.type === 1 && item.text_item && typeof item.text_item.text === 'string') {
      const t = item.text_item.text.trim();
      if (t) return { kind: 'text', text: t };
    }
    if (item.type === 3 && item.voice_item && typeof item.voice_item.text === 'string') {
      const t = item.voice_item.text.trim();
      if (t) return { kind: 'text', text: t };
    }
  }
  return { kind: 'unsupported', text: '' };
}

function createDedupStore(state, { memoryMax = 2000, persistMax = 1000 } = {}) {
  const saved = state.readDedup();
  const initial = Array.isArray(saved && saved.ids) ? saved.ids.slice(-persistMax) : [];
  const lru = new Set(initial);

  function has(id) {
    if (!id) return false;
    return lru.has(String(id));
  }

  function add(id) {
    if (!id) return;
    const key = String(id);
    if (lru.has(key)) {
      lru.delete(key);
    }
    lru.add(key);
    while (lru.size > memoryMax) {
      const oldest = lru.values().next().value;
      lru.delete(oldest);
    }
    const persisted = Array.from(lru).slice(-persistMax);
    state.writeDedup({ ids: persisted });
  }

  return { has, add };
}

function abortableSleep(ms, signal) {
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

async function runMonitor({
  client,
  state,
  account,
  instanceId,
  api,
  log = console,
  signal,
  maxQueue = Number(process.env.ILINK_MAX_QUEUE || 8),
  replyChunk = Number(process.env.ILINK_REPLY_CHUNK || 1800),
  pollTaskOpts = {},
  onContextUpdated = () => {},
  sleepImpl,
  nowFn = () => Date.now(),
} = {}) {
  const waitBackoff = sleepImpl || ((ms) => abortableSleep(ms, signal));
  const dedup = createDedupStore(state);
  let cursor = (state.readCursor() && state.readCursor().get_updates_buf) || '';
  let backoffMs = 2000;
  let lastUnsupportedNoticeAt = 0;
  let lastQueueFullNoticeAt = 0;

  const queue = [];
  let processing = false;

  async function sendWithRetry(toUserId, contextToken, text) {
    const chunks = splitReplyChunks(text, replyChunk);
    for (const chunk of chunks) {
      try {
        await client.sendText({ account, toUserId, contextToken, text: chunk, signal });
      } catch (err) {
        if (err instanceof IlinkSessionExpiredError) throw err;
        await client.sendText({ account, toUserId, contextToken, text: chunk, signal });
      }
    }
  }

  async function drainQueue() {
    if (processing) return;
    processing = true;
    try {
      while (queue.length > 0 && (!signal || !signal.aborted)) {
        const job = queue.shift();
        try {
          const chatRes = await api.chat(instanceId, job.imUserId, job.text);
          let reply = '';
          if (chatRes && chatRes.task_id) {
            const taskRes = await api.pollTask(instanceId, chatRes.task_id, {
              ...pollTaskOpts,
              signal,
            });
            reply = (taskRes && taskRes.reply) || '';
          } else if (chatRes && chatRes.reply) {
            reply = chatRes.reply;
          }
          if (reply) {
            await sendWithRetry(job.fromUserId, job.contextToken, reply);
          }
        } catch (err) {
          if (err instanceof IlinkSessionExpiredError) {
            await api
              .updateStatus(instanceId, {
                status: 'session_expired',
                last_error: '微信登录态已过期，请重新扫码登录',
              })
              .catch(() => {});
            throw err;
          }
          log.error?.('[ilink-monitor] message processing failed:', err.message);
          await sendWithRetry(
            job.fromUserId,
            job.contextToken,
            `查询失败：${err.message || '服务暂时不可用'}`
          ).catch(() => {});
        }
      }
    } finally {
      processing = false;
    }
  }

  let fatalError = null;

  while (!signal || !signal.aborted) {
    if (fatalError) {
      throw fatalError;
    }
    let res;
    try {
      res = await client.getUpdates({ account, cursor, signal });
      backoffMs = 2000;
    } catch (err) {
      if (signal && signal.aborted) break;
      if (err instanceof IlinkSessionExpiredError) {
        await api
          .updateStatus(instanceId, {
            status: 'session_expired',
            last_error: '微信登录态已过期，请重新扫码登录',
          })
          .catch(() => {});
        throw err;
      }
      if (err instanceof IlinkTimeoutError) {
        continue;
      }
      log.warn?.('[ilink-monitor] getupdates error:', err.message);
      await waitBackoff(backoffMs);
      if (signal && signal.aborted) break;
      backoffMs = Math.min(backoffMs * 2, 60000);
      continue;
    }

    if (res && typeof res.get_updates_buf === 'string' && res.get_updates_buf && res.get_updates_buf !== cursor) {
      cursor = res.get_updates_buf;
      state.writeCursor({ get_updates_buf: cursor });
    }

    const msgs = Array.isArray(res && res.msgs) ? res.msgs : [];
    for (const msg of msgs) {
      if (!msg) continue;
      if (msg.message_type === 2) continue;
      if (msg.group_id) continue;
      if (!msg.from_user_id) continue;
      if (account.ilink_user_id && msg.from_user_id !== account.ilink_user_id) continue;

      if (msg.message_id && dedup.has(msg.message_id)) continue;
      if (msg.message_id) dedup.add(msg.message_id);

      if (msg.context_token) {
        const updatedAt = new Date(nowFn()).toISOString();
        state.writeContext({
          context_token: msg.context_token,
          from_user_id: msg.from_user_id,
          updated_at: updatedAt,
          invalidated: false,
        });
        try {
          if (typeof onContextUpdated === 'function') {
            onContextUpdated({ updatedAt, contextToken: msg.context_token });
          }
        } catch {
          // Ignore wake callback errors
        }
      }

      const extracted = extractMessageText(msg);
      const contextToken = msg.context_token || '';
      if (extracted.kind !== 'text') {
        const now = nowFn();
        if (now - lastUnsupportedNoticeAt >= 600000) {
          lastUnsupportedNoticeAt = now;
          await sendWithRetry(msg.from_user_id, contextToken, '目前仅支持文字消息').catch((err) => {
            if (err instanceof IlinkSessionExpiredError) fatalError = err;
          });
        }
        continue;
      }

      if (queue.length >= maxQueue) {
        const now = nowFn();
        if (now - lastQueueFullNoticeAt >= 60000) {
          lastQueueFullNoticeAt = now;
          await sendWithRetry(msg.from_user_id, contextToken, '消息过多，请稍后再试').catch((err) => {
            if (err instanceof IlinkSessionExpiredError) fatalError = err;
          });
        }
        continue;
      }

      queue.push({
        fromUserId: msg.from_user_id,
        imUserId: `ilink:${msg.from_user_id}`,
        contextToken,
        text: extracted.text,
      });
      drainQueue().catch((err) => {
        fatalError = err;
      });
    }
  }

  if (fatalError) {
    throw fatalError;
  }
}

module.exports = {
  splitReplyChunks,
  extractMessageText,
  createDedupStore,
  runMonitor,
};
