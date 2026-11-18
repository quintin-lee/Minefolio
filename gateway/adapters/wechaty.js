/**
 * wechaty adapter (WeChat personal account via wechaty + a puppet).
 *
 * Optional-dependency adapter: `wechaty` is not installed by default. Load
 * this module only after `npm i wechaty <puppet>`; it throws a clear message
 * otherwise. The bridge core is identical to the OneBot adapter — feed
 * handleImMessage and return the reply.
 *
 * Usage:
 *   const { createWechatyBot } = require('./wechaty.js');
 *   await createWechatyBot({ puppet: '@wechaty/puppet-wechat4', getBindToken, token });
 *
 * P1 scope: 1:1 chat only (room messages ignored). 个人号风控敏感，保持低频、只读。
 */

const { handleImMessage } = require('../index.js');

async function createWechatyBot({ puppet, getBindToken, token, log }) {
  let wechaty;
  try {
    wechaty = require('wechaty');
  } catch {
    throw new Error(
      'wechaty is not installed. Run: npm i wechaty ' + (puppet ? puppet : '<puppet>'),
    );
  }

  const logger =
    log ||
    {
      info: (...a) => console.log('[wechaty]', ...a),
      warn: (...a) => console.warn('[wechaty]', ...a),
    };

  const bot = wechaty.default || wechaty;
  if (token) bot.token(token);

  bot.on('message', async (msg) => {
    try {
      if (msg.self() || msg.room()) {
        return; // P1: 忽略自发自收与群聊
      }
      const imUserId = msg.talker().id;
      const text = msg.text().trim();
      if (!text) return;
      const bindToken = await getBindToken(imUserId);
      const reply = await handleImMessage('wechat', imUserId, text, bindToken);
      logger.info('wechat', imUserId, '<-', text, '->', reply);
      await msg.say(reply);
    } catch (e) {
      logger.warn('message handler error', e && e.message);
    }
  });

  await bot.start();
  logger.info('wechaty bot started (puppet:', puppet || 'default', ')');
  return bot;
}

module.exports = { createWechatyBot };
