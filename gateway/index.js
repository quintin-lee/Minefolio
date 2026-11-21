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
const TOKEN_CACHE = new Map(); // `${platform}:${imUserId}` -> bindToken

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
 * Poll GET /api/im/tasks/:id until completion or failure.
 * @param {number} taskId
 * @param {string} bearer
 * @param {{intervalMs?:number, timeoutMs?:number}} [opts]
 */
async function pollImTask(taskId, bearer, opts) {
  const intervalMs = Math.max(20, (opts && opts.intervalMs) || 150);
  const timeoutMs = Math.max(1000, (opts && opts.timeoutMs) || 60000);
  const deadline = Date.now() + timeoutMs;
  const baseUrl = process.env.MINEFOLIO_API || API_BASE;

  while (true) {
    const res = await fetch(`${baseUrl}/api/im/tasks/${taskId}`, {
      method: 'GET',
      headers: bearer ? { Authorization: `Bearer ${bearer}` } : {},
    });
    let json = null;
    try {
      json = await res.json();
    } catch {
      json = null;
    }
    if (!json || json.code !== 0 || !json.data) {
      return {
        code: json ? json.code : -1,
        message: (json && json.message) || 'poll failed',
        http: res.status,
      };
    }
    const task = json.data;
    if (task.status === 'completed') {
      return { code: 0, data: task.result || {}, http: res.status };
    }
    if (task.status === 'failed' || task.status === 'cancelled') {
      return {
        code: 500,
        message: task.error_message || task.message || '后台任务执行失败',
        http: res.status,
      };
    }
    if (Date.now() >= deadline) {
      return { code: 504, message: '查询超时，请稍后再试', http: 504 };
    }
    await new Promise((r) => setTimeout(r, intervalMs));
  }
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
    if (r.data && typeof r.data.reply === 'string') {
      return r.data.reply || '（无回复）';
    }
    if (r.data && Number(r.data.task_id) > 0) {
      const polled = await pollImTask(Number(r.data.task_id), activeToken);
      if (polled.code === 0) {
        return (polled.data && polled.data.reply) || '（无回复）';
      }
      return `查询失败: ${polled.message || ('HTTP ' + polled.http)}`;
    }
    return '（无回复）';
  }
  // Backend-level guidance (unbound / forbidden) vs transport error.
  if (r.http === 401 || r.code === 1001) {
    TOKEN_CACHE.delete(`${platform}:${imUserId}`);
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

module.exports = { handleImMessage, refreshBindToken, callImApi, pollImTask, API_BASE };
