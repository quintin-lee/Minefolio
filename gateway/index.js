/**
 * Minefolio IM Bot Gateway (core)
 *
 * Bridges IM platform messages (QQ / wechat) to the read-only backend
 * endpoints /api/im/{platform}/chat and /api/im/{platform}/bind.
 *
 * Zero external dependencies: uses Node's built-in fetch (Node >= 18).
 *
 * Contract (aligns with backend im_bot_controller.c):
 *   - The bind token must belong to the Minefolio user that owns the
 *     im_user_id (enforced by the backend's bound==user_id check).
 *   - /chat body: { im_user_id, message }  -> { code, data: { reply, bound } }
 *   - /bind body: { im_user_id }           -> { code, data: { message } }
 *
 * This module is transport-agnostic: IM adapters (NapCat/OneBot, wechaty,
 * or the CLI in cli.js) call `handleImMessage()` with the incoming text and
 * get a reply string back.
 */

const API_BASE = process.env.MINEFOLIO_API || 'http://localhost:8080';

/**
 * Call a backend /api/im/* endpoint with a Bearer bind token.
 * @param {string} platform  'qq' | 'wechat'
 * @param {'chat'|'bind'|'token'} op
 * @param {object} body
 * @param {string} bearer     bind token (im_bind JWT) or App token
 * @returns {Promise<{code:number, data?:object, message?:string, http:number}>}
 */
async function callImApi(platform, op, body, bearer) {
  const path =
    op === 'chat' ? `/api/im/${platform}/chat` :
    op === 'bind' ? `/api/im/${platform}/bind` :
    '/api/im/bindings/token';

  const res = await fetch(API_BASE + path, {
    method: op === 'token' ? 'POST' : 'POST',
    headers: {
      'Content-Type': 'application/json',
      ...(bearer ? { Authorization: `Bearer ${bearer}` } : {}),
    },
    body: JSON.stringify(body || {}),
  });
  let json = null;
  try {
    json = await res.json();
  } catch {
    json = null;
  }
  return { code: json ? json.code : -1, data: json ? json.data : null, message: json ? json.message : 'empty response', http: res.status };
}

/**
 * Handle an incoming IM message and return the reply string.
 *
 * - Text starting with `/bind <code>` triggers a bind using that code as the
 *   bearer (the code IS the user-level bind token the App generated).
 * - Any other text is a read-only query via /chat using the supplied bearer.
 *
 * @param {string} platform  'qq' | 'wechat'
 * @param {string} imUserId  the IM account id (QQ uid / wechat user)
 * @param {string} text      incoming message text
 * @param {string} bindToken bearer for /chat (must own imUserId on the backend)
 * @returns {Promise<string>} reply to send back to the IM user
 */
async function handleImMessage(platform, imUserId, text, bindToken) {
  const trimmed = String(text || '').trim();

  // /bind <code>: the code is a fresh App-generated bind token for this user.
  if (/^\/bind\b/.test(trimmed)) {
    const code = trimmed.replace(/^\/bind\b/, '').trim();
    if (!code) {
      return '用法: /bind <绑定码>（在 App「IM 机器人」里生成）';
    }
    const r = await callImApi(platform, 'bind', { im_user_id: imUserId }, code);
    if (r.code === 0) {
      return r.data && r.data.message ? r.data.message : '绑定成功，现在可以问我持仓/汇率了';
    }
    return `绑定失败: ${r.message || ('HTTP ' + r.http)}`;
  }

  // Read-only query.
  if (!bindToken) {
    return '尚未绑定。请先到 App「IM 机器人」生成绑定码，发送 /bind <码> 完成绑定。';
  }
  const r = await callImApi(platform, 'chat', { im_user_id: imUserId, message: trimmed }, bindToken);
  if (r.code === 0) {
    return (r.data && r.data.reply) || '（无回复）';
  }
  // Backend-level guidance (unbound / forbidden) vs transport error.
  if (r.http === 401 || r.code === 1001) {
    return '绑定码已失效，请重新在 App 生成绑定码并 /bind。';
  }
  return `查询失败: ${r.message || ('HTTP ' + r.http)}`;
}

/**
 * Fetch a fresh bind token for a user's App session (used by adapters that
 * hold the App token, not the bind code). Returns the token string or null.
 * @param {string} appToken  a normal (non-bind) App access token
 * @returns {Promise<string|null>}
 */
async function refreshBindToken(appToken) {
  const r = await callImApi(null, 'token', {}, appToken);
  if (r.code === 0 && r.data && r.data.bind_token) {
    return r.data.bind_token;
  }
  return null;
}

module.exports = { handleImMessage, refreshBindToken, callImApi, API_BASE };
