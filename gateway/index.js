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
const TOKEN_CACHE = new Map(); // `${platform}:${imUserId}` -> { token, expiresAt }
const DEFAULT_TOKEN_CACHE_MAX = 2000;
const DEFAULT_TOKEN_CACHE_TTL_MS = 24 * 60 * 60 * 1000;
const PLATFORM_RE = /^(qq|wechat)$/;
const DEFAULT_HTTP_TIMEOUT_MS = 15000;
const CACHE_FILE = process.env.MINEFOLIO_IM_CACHE_FILE || path.join(__dirname, 'im_bindings.json');

function getTokenCacheMax() {
  const raw = Number(process.env.MINEFOLIO_GATEWAY_TOKEN_CACHE_MAX);
  return Number.isFinite(raw) && raw >= 1 ? Math.floor(raw) : DEFAULT_TOKEN_CACHE_MAX;
}

function getTokenCacheTtlMs() {
  const raw = Number(process.env.MINEFOLIO_GATEWAY_TOKEN_CACHE_TTL_MS);
  return Number.isFinite(raw) && raw >= 10 ? Math.floor(raw) : DEFAULT_TOKEN_CACHE_TTL_MS;
}

function purgeExpiredTokens(now = Date.now()) {
  for (const [k, entry] of TOKEN_CACHE.entries()) {
    if (!entry || typeof entry !== 'object' || now >= entry.expiresAt) {
      TOKEN_CACHE.delete(k);
    }
  }
}

function saveCache() {
  try {
    const obj = {};
    for (const [k, v] of TOKEN_CACHE.entries()) {
      if (v && typeof v === 'object' && typeof v.token === 'string') {
        obj[k] = v;
      } else if (typeof v === 'string') {
        obj[k] = { token: v, expiresAt: Date.now() + getTokenCacheTtlMs() };
      }
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

function loadCache() {
  try {
    if (fs.existsSync(CACHE_FILE)) {
      const data = JSON.parse(fs.readFileSync(CACHE_FILE, 'utf8'));
      if (data && typeof data === 'object') {
        const now = Date.now();
        for (const [k, v] of Object.entries(data)) {
          if (v && typeof v === 'object' && typeof v.token === 'string') {
            if (typeof v.expiresAt === 'number' && v.expiresAt > now) {
              TOKEN_CACHE.set(k, v);
            }
          } else if (typeof v === 'string' && v) {
            TOKEN_CACHE.set(k, { token: v, expiresAt: now + getTokenCacheTtlMs() });
          }
        }
      }
    }
  } catch (err) {
    console.warn('[gateway] loadCache failed:', err && err.message);
  }
}

function getCachedToken(key) {
  if (!TOKEN_CACHE.has(key)) {
    return undefined;
  }
  const entry = TOKEN_CACHE.get(key);
  if (typeof entry === 'string') {
    return entry;
  }
  const now = Date.now();
  if (!entry || typeof entry !== 'object' || now >= entry.expiresAt) {
    TOKEN_CACHE.delete(key);
    saveCache();
    return undefined;
  }
  TOKEN_CACHE.delete(key);
  TOKEN_CACHE.set(key, entry);
  return entry.token;
}

function setCachedToken(key, val, ttlMs = getTokenCacheTtlMs()) {
  const now = Date.now();
  purgeExpiredTokens(now);
  const maxEntries = getTokenCacheMax();
  if (TOKEN_CACHE.has(key)) {
    TOKEN_CACHE.delete(key);
  }
  while (TOKEN_CACHE.size >= maxEntries) {
    const oldestKey = TOKEN_CACHE.keys().next().value;
    if (oldestKey === undefined) {
      break;
    }
    TOKEN_CACHE.delete(oldestKey);
  }
  TOKEN_CACHE.set(key, { token: val, expiresAt: now + ttlMs });
  saveCache();
}

function getHttpTimeoutMs() {
  const raw = Number(process.env.MINEFOLIO_HTTP_TIMEOUT_MS);
  return Number.isFinite(raw) && raw >= 50 ? raw : DEFAULT_HTTP_TIMEOUT_MS;
}

function isValidPlatform(platform) {
  return typeof platform === 'string' && PLATFORM_RE.test(platform);
}

let cachedCsrfToken = '';
let csrfTokenExpiresAt = 0;

async function getCsrfToken() {
  const now = Date.now();
  if (cachedCsrfToken && now < csrfTokenExpiresAt) {
    return cachedCsrfToken;
  }
  try {
    const res = await fetch((process.env.MINEFOLIO_API || API_BASE) + '/api/system/status', {
      signal: AbortSignal.timeout(3000),
    });
    const raw = res.headers.get('set-cookie');
    const match = raw ? raw.match(/csrf_token=([^;]+)/) : null;
    if (match && match[1]) {
      cachedCsrfToken = match[1];
      csrfTokenExpiresAt = now + 3600 * 1000;
      return cachedCsrfToken;
    }
  } catch {
    // fallback if system status unreachable
  }
  return '';
}

loadCache();

/**
 * Call a backend /api/im/* endpoint with a Bearer bind token.
 * @param {string|null} platform  'qq' | 'wechat' (null only when op === 'token')
 * @param {'chat'|'bind'|'bind-auth'|'token'} op
 * @param {object} body
 * @param {string} bearer     bind token (im_bind JWT) or App token
 * @returns {Promise<{code:number, data?:object, message?:string, http:number}>}
 */
async function callImApi(platform, op, body, bearer) {
  if (op !== 'token' && !isValidPlatform(platform)) {
    return { code: 1002, data: null, message: 'invalid platform', http: 400 };
  }

  const path =
    op === 'chat' ? `/api/im/${platform}/chat` :
    op === 'bind' ? `/api/im/${platform}/bind` :
    op === 'bind-auth' ? `/api/im/${platform}/bind-auth` :
    op === 'token' ? '/api/im/bindings/token' :
    null;

  if (!path) {
    return { code: 1002, data: null, message: 'invalid operation', http: 400 };
  }

  const headers = {
    'Content-Type': 'application/json',
  };
  if (bearer) {
    headers['Authorization'] = `Bearer ${bearer}`;
  } else {
    const csrf = await getCsrfToken();
    if (csrf) {
      headers['Cookie'] = `csrf_token=${csrf}`;
      headers['X-CSRF-Token'] = csrf;
    }
  }

  let res;
  try {
    res = await fetch((process.env.MINEFOLIO_API || API_BASE) + path, {
      method: 'POST',
      headers,
      body: JSON.stringify(body || {}),
      signal: AbortSignal.timeout(getHttpTimeoutMs()),
    });
    if (res.status === 403 && !bearer) {
      cachedCsrfToken = '';
      const freshCsrf = await getCsrfToken();
      if (freshCsrf) {
        headers['Cookie'] = `csrf_token=${freshCsrf}`;
        headers['X-CSRF-Token'] = freshCsrf;
        res = await fetch((process.env.MINEFOLIO_API || API_BASE) + path, {
          method: 'POST',
          headers,
          body: JSON.stringify(body || {}),
          signal: AbortSignal.timeout(getHttpTimeoutMs()),
        });
      }
    }
  } catch (err) {
    const isTimeout = err && (err.name === 'TimeoutError' || err.name === 'AbortError');
    return {
      code: isTimeout ? 504 : -1,
      data: null,
      message: isTimeout ? 'request timeout' : (err && err.message) || 'network error',
      http: isTimeout ? 504 : 502,
    };
  }

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
    const remainingMs = deadline - Date.now();
    if (remainingMs <= 0) {
      return { code: 504, message: '查询超时，请稍后再试', http: 504 };
    }
    const perReqTimeoutMs = Math.min(getHttpTimeoutMs(), Math.max(50, remainingMs));

    let res;
    try {
      res = await fetch(`${baseUrl}/api/im/tasks/${taskId}`, {
        method: 'GET',
        headers: bearer ? { Authorization: `Bearer ${bearer}` } : {},
        signal: AbortSignal.timeout(perReqTimeoutMs),
      });
    } catch (err) {
      const isTimeout = err && (err.name === 'TimeoutError' || err.name === 'AbortError');
      return {
        code: isTimeout ? 504 : -1,
        message: isTimeout ? '查询超时，请稍后再试' : ((err && err.message) || 'network error'),
        http: isTimeout ? 504 : 502,
      };
    }

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
  if (!isValidPlatform(platform)) {
    return '不支持的 IM 平台';
  }
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
        savedToken = (r.data && r.data.bind_token) || code;
      }
    }

    if (r.code === 0) {
      if (savedToken) {
        const ttl = (r.data && r.data.bind_token) ? 365 * 86400 * 1000 : getTokenCacheTtlMs();
        setCachedToken(`${platform}:${imUserId}`, savedToken, ttl);
      }
      return r.data && r.data.message ? r.data.message : '绑定成功，现在可以问我持仓/汇率了';
    }
    return `绑定失败: ${r.message || ('HTTP ' + r.http)}`;
  }

  // Read-only query.
  const activeToken = bindToken || getCachedToken(`${platform}:${imUserId}`);
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
  pollImTask,
  API_BASE,
  TOKEN_CACHE,
  loadCache,
  saveCache,
  CACHE_FILE,
  getCachedToken,
  setCachedToken,
};
