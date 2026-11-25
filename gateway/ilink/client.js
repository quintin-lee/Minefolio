'use strict';

const crypto = require('crypto');

const BASE_URL = 'https://ilinkai.weixin.qq.com';
const APP_ID = 'bot';
const CLIENT_VERSION = '196608';
const CHANNEL_VERSION = '3.0.0-minefolio';
const BOT_AGENT = 'Minefolio/0.1.0';

class IlinkSessionExpiredError extends Error {
  constructor(message = 'iLink session expired (-14)') {
    super(message);
    this.name = 'IlinkSessionExpiredError';
    this.code = -14;
  }
}

class IlinkTimeoutError extends Error {
  constructor(message = 'iLink request timed out') {
    super(message);
    this.name = 'IlinkTimeoutError';
  }
}

function randomWechatUin() {
  const num = crypto.randomBytes(4).readUInt32BE(0);
  return Buffer.from(String(num), 'utf8').toString('base64');
}

function buildHeaders({ token, commonOnly = false, bodyBytes } = {}) {
  const headers = {
    'Content-Type': 'application/json',
    'X-WECHAT-UIN': randomWechatUin(),
    'iLink-App-Id': APP_ID,
    'iLink-App-ClientVersion': CLIENT_VERSION,
    'User-Agent': BOT_AGENT,
  };
  if (!commonOnly && token) {
    headers.AuthorizationType = 'ilink_bot_token';
    headers.Authorization = `Bearer ${token}`;
  }
  if (bodyBytes !== undefined && bodyBytes !== null) {
    headers['Content-Length'] = String(bodyBytes);
  }
  return headers;
}

function baseInfo() {
  return {
    channel_version: CHANNEL_VERSION,
  };
}

function isSessionExpiredPayload(data) {
  return Boolean(data && (data.ret === -14 || data.errcode === -14));
}

function isTimeoutErr(err) {
  if (!err) return false;
  if (err.name === 'TimeoutError' || err.name === 'AbortError') return true;
  if (err.code === 'ABORT_ERR' || err.code === 'ETIMEDOUT') return true;
  return false;
}

function createIlinkClient({
  fetchImpl = fetch,
  baseUrl = BASE_URL,
  timeoutMs = Number(process.env.ILINK_HTTP_TIMEOUT_MS || 45000),
} = {}) {
  const defaultBase = String(baseUrl).replace(/\/+$/, '');

  async function requestJson(url, { method = 'GET', headers, body, reqTimeoutMs, signal } = {}) {
    const signals = [AbortSignal.timeout(reqTimeoutMs || timeoutMs)];
    if (signal) signals.push(signal);
    const combinedSignal =
      typeof AbortSignal.any === 'function' ? AbortSignal.any(signals) : signals[0];
    let res;
    try {
      res = await fetchImpl(url, {
        method,
        headers,
        body,
        signal: combinedSignal,
      });
    } catch (err) {
      if (isTimeoutErr(err) && !(signal && signal.aborted)) {
        throw new IlinkTimeoutError(err.message);
      }
      throw err;
    }
    const text = await res.text();
    let data = null;
    if (text) {
      try {
        data = JSON.parse(text);
      } catch {
        throw new Error(`iLink non-JSON response (${res.status})`);
      }
    }
    if (!res.ok) {
      if (isSessionExpiredPayload(data)) {
        throw new IlinkSessionExpiredError(data.errmsg || 'iLink session expired');
      }
      throw new Error(`iLink HTTP ${res.status}: ${(data && data.errmsg) || 'request failed'}`);
    }
    if (isSessionExpiredPayload(data)) {
      throw new IlinkSessionExpiredError(data.errmsg || 'iLink session expired');
    }
    return data || {};
  }

  async function getBotQrcode({ baseUrl: overrideBase, timeoutMs: reqTimeoutMs, signal } = {}) {
    const root = String(overrideBase || defaultBase).replace(/\/+$/, '');
    const headers = buildHeaders({ commonOnly: true });
    return requestJson(`${root}/ilink/bot/get_bot_qrcode?bot_type=3`, {
      method: 'GET',
      headers,
      reqTimeoutMs: reqTimeoutMs || 15000,
      signal,
    });
  }

  async function getQrcodeStatus({
    qrcode,
    verifyCode,
    baseUrl: overrideBase,
    timeoutMs: reqTimeoutMs = 40000,
    signal,
  } = {}) {
    const root = String(overrideBase || defaultBase).replace(/\/+$/, '');
    const params = new URLSearchParams({ qrcode: String(qrcode || '') });
    if (verifyCode) {
      params.set('verify_code', String(verifyCode));
    }
    const headers = buildHeaders({ commonOnly: true });
    return requestJson(`${root}/ilink/bot/get_qrcode_status?${params.toString()}`, {
      method: 'GET',
      headers,
      reqTimeoutMs,
      signal,
    });
  }

  async function getUpdates({ account, cursor = '', timeoutMs: reqTimeoutMs, signal } = {}) {
    const root = String((account && account.baseurl) || defaultBase).replace(/\/+$/, '');
    const payload = JSON.stringify({
      get_updates_buf: cursor || '',
      base_info: baseInfo(),
    });
    const headers = buildHeaders({
      token: account && account.bot_token,
      bodyBytes: Buffer.byteLength(payload, 'utf8'),
    });
    return requestJson(`${root}/ilink/bot/getupdates`, {
      method: 'POST',
      headers,
      body: payload,
      reqTimeoutMs: reqTimeoutMs || timeoutMs,
      signal,
    });
  }

  async function sendText({
    account,
    toUserId,
    contextToken = '',
    text,
    clientId,
    timeoutMs: reqTimeoutMs,
    signal,
  } = {}) {
    const root = String((account && account.baseurl) || defaultBase).replace(/\/+$/, '');
    const payload = JSON.stringify({
      msg: {
        from_user_id: '',
        to_user_id: String(toUserId || ''),
        client_id: clientId || crypto.randomUUID(),
        message_type: 2,
        message_state: 2,
        context_token: String(contextToken || ''),
        item_list: [
          {
            type: 1,
            text_item: {
              text: String(text || ''),
            },
          },
        ],
      },
      base_info: baseInfo(),
    });
    const headers = buildHeaders({
      token: account && account.bot_token,
      bodyBytes: Buffer.byteLength(payload, 'utf8'),
    });
    return requestJson(`${root}/ilink/bot/sendmessage`, {
      method: 'POST',
      headers,
      body: payload,
      reqTimeoutMs: reqTimeoutMs || 15000,
      signal,
    });
  }

  return {
    getBotQrcode,
    getQrcodeStatus,
    getUpdates,
    sendText,
  };
}

module.exports = {
  BASE_URL,
  APP_ID,
  CLIENT_VERSION,
  CHANNEL_VERSION,
  BOT_AGENT,
  IlinkSessionExpiredError,
  IlinkTimeoutError,
  buildHeaders,
  baseInfo,
  isSessionExpiredPayload,
  createIlinkClient,
};
