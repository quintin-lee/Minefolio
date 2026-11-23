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

const fs = require('fs');
const path = require('path');

const API_BASE = process.env.MINEFOLIO_API || 'http://localhost:8080';
const TOKEN_CACHE = new Map(); // `${platform}:${imUserId}` -> bindToken
const CACHE_FILE = process.env.MINEFOLIO_IM_CACHE_FILE || path.join(__dirname, 'im_bindings.json');

function loadCache() {
  try {
    if (fs.existsSync(CACHE_FILE)) {
      const data = JSON.parse(fs.readFileSync(CACHE_FILE, 'utf8'));
      if (data && typeof data === 'object') {
        for (const [k, v] of Object.entries(data)) {
          if (typeof v === 'string' && v) {
            TOKEN_CACHE.set(k, v);
          }
        }
      }
    }
  } catch (err) {
    console.warn('[gateway] loadCache failed:', err && err.message);
  }
}

function saveCache() {
  try {
    const obj = {};
    for (const [k, v] of TOKEN_CACHE.entries()) {
      obj[k] = v;
    }
    const dir = path.dirname(CACHE_FILE);
    if (!fs.existsSync(dir)) {
      fs.mkdirSync(dir, { recursive: true });
    }
    const tmp = `${CACHE_FILE}.tmp.${Date.now()}`;
    fs.writeFileSync(tmp, JSON.stringify(obj, null, 2), 'utf8');
    fs.renameSync(tmp, CACHE_FILE);
  } catch (err) {
    console.warn('[gateway] saveCache failed:', err && err.message);
  }
}

loadCache();

/**
 * Call a backend /api/im/* endpoint with a Bearer bind token.
 * @param {string} platform  'qq' | 'wechat'
 * @param {'chat'|'bind'|'bind-auth'|'token'} op
 * @param {object} body
 * @param {string} bearer     bind token (im_bind JWT) or App token
 * @returns {Promise<{code:number, data?:object, message?:string, http:number}>}
 */
async function callImApi(platform, op, body, bearer) {
  const path =
    op === 'chat' ? `/api/im/${platform}/chat` :
    op === 'bind' ? `/api/im/${platform}/bind` :
    op === 'bind-auth' ? `/api/im/${platform}/bind-auth` :
    '/api/im/bindings/token';

  const res = await fetch((process.env.MINEFOLIO_API || API_BASE) + path, {
    method: 'POST',
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
 * - Text starting with `/bind <code>` or `/bind <username> <password>` triggers binding.
 * - Any other text is a read-only query via /chat using the supplied bearer or cached token.
 *
 * @param {string} platform  'qq' | 'wechat'
 * @param {string} imUserId  the IM account id (QQ uid / wechat user)
 * @param {string} text      incoming message text
 * @param {string} bindToken bearer for /chat (must own imUserId on the backend)
 * @returns {Promise<string>} reply to send back to the IM user
 */
async function handleImMessage(platform, imUserId, text, bindToken) {
  const trimmed = String(text || '').trim();

  // /bind <code> or /bind <username> <password>
  if (/^\/bind\b/.test(trimmed)) {
    const rawArgs = trimmed.replace(/^\/bind\b/, '').trim();
    if (!rawArgs) {
      return '用法: /bind <绑定码>（在 App「IM 机器人」里生成），或 /bind <用户名> <密码>';
    }

    const parts = rawArgs.split(/\s+/);
    let r;
    let savedToken = null;

    if (parts.length >= 2) {
      // Self-bind via username & password (P2-A)
      const username = parts[0];
      const password = parts.slice(1).join(' ');
      r = await callImApi(platform, 'bind-auth', { im_user_id: imUserId, username, password });
      if (r.code === 0 && r.data && r.data.bind_token) {
        savedToken = r.data.bind_token;
      }
    } else {
      // Bind via token generated in App (P0)
      const code = parts[0];
      r = await callImApi(platform, 'bind', { im_user_id: imUserId }, code);
      if (r.code === 0) {
        savedToken = code;
      }
    }

    if (r.code === 0) {
      if (savedToken) {
        TOKEN_CACHE.set(`${platform}:${imUserId}`, savedToken);
        saveCache();
      }
      return r.data && r.data.message ? r.data.message : '绑定成功，现在可以问我持仓/汇率了';
    }
    return `绑定失败: ${r.message || ('HTTP ' + r.http)}`;
  }

  // Read-only query.
  const activeToken = bindToken || TOKEN_CACHE.get(`${platform}:${imUserId}`);
  if (!activeToken) {
    return '尚未绑定。请先到 App「IM 机器人」生成绑定码，发送 /bind <码> 完成绑定，或发送 /bind <用户名> <密码>。';
  }
  const r = await callImApi(platform, 'chat', { im_user_id: imUserId, message: trimmed }, activeToken);
  if (r.code === 0) {
    return (r.data && r.data.reply) || '（无回复）';
  }
  // Backend-level guidance (unbound / forbidden) vs transport error.
  if (r.http === 401 || r.code === 1001) {
    TOKEN_CACHE.delete(`${platform}:${imUserId}`);
    saveCache();
    return '绑定码已失效，请重新在 App 生成绑定码并 /bind，或发送 /bind <用户名> <密码>。';
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

module.exports = {
  handleImMessage,
  refreshBindToken,
  callImApi,
  API_BASE,
  TOKEN_CACHE,
  loadCache,
  saveCache,
  CACHE_FILE,
};
