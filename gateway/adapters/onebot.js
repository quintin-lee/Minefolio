/**
 * OneBot v11 adapter (QQ platforms: NapCat, LLObot, go-cqhttp).
 *
 * All OneBot v11 implementations speak the same protocol, so one adapter
 * covers them. The bridge core is transport-agnostic: it receives an
 * `onEvent(event)` and replies via an injected `transport.sendPrivateMsg`.
 * This keeps the protocol logic testable with a mock transport/events and
 * runnable against a real NapCat/OneBot endpoint.
 *
 * Design:
 *   - only private (message_type === 'private') `message` events are bridged;
 *     group messages are ignored (P1 scope = 1:1 查账).
 *   - getBindToken(imUserId) is an injected function: P1 resolves it from a
 *     static map (env/config); P2-A replaces it with a backend bind-auth call.
 *   - the reply string from handleImMessage is sent back to the same user.
 */

const { handleImMessage } = require('../index.js');

/**
 * Build a OneBot v11 QQ adapter.
 * @param {object} opts
 * @param {(imUserId:string)=>(string|Promise<string>)} opts.getBindToken
 *        resolve the im_bind token for a given QQ uid; return '' if unbound.
 * @param {{sendPrivateMsg:(uid:string, text:string)=>Promise<void>}} opts.transport
 *        reply channel (OneBot send_private_msg, or a mock in tests).
 * @param {object} [opts.log]  optional {info,warn} logger (defaults to console).
 * @returns {{ onEvent:(event:object)=>Promise<void>, platform:string }}
 */
function redactLogText(text) {
  if (/^\s*\/bind(?:\s|$)/i.test(text)) {
    return '/bind [REDACTED]';
  }
  return text;
}

function createOneBotAdapter({ getBindToken, transport, log }) {
  const logger = log || { info: (...a) => console.log('[onebot]', ...a), warn: (...a) => console.warn('[onebot]', ...a) };

  async function onEvent(event) {
    if (!event || event.post_type !== 'message') {
      return;
    }
    if (event.message_type !== 'private') {
      return; // P1: 私聊 only
    }
    const imUserId = String(event.sender && event.sender.user_id || event.user_id || '');
    if (!imUserId) {
      return;
    }
    // OneBot 文本消息: event.raw_message (plain text). 非文本/系统消息跳过。
    const text = String(event.raw_message || '').trim();
    if (!text) {
      return;
    }
    const bindToken = await getBindToken(imUserId);
    const reply = await handleImMessage('qq', imUserId, text, bindToken);
    logger.info('qq', imUserId, '<-', redactLogText(text), '->', reply);
    await transport.sendPrivateMsg(imUserId, reply);
  }

  return { onEvent, platform: 'qq' };
}

module.exports = { createOneBotAdapter };
