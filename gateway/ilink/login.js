'use strict';

const { IlinkTimeoutError } = require('./client');

function normalizeQrContent(res) {
  return res.qrcode_img_content || res.qrcode_url || res.qr_content || res.qrcode || '';
}

function isAllowedWeixinHost(hostname) {
  if (!hostname || hostname.startsWith('.') || hostname.includes('..')) {
    return false;
  }
  return hostname === 'weixin.qq.com' || hostname.endsWith('.weixin.qq.com');
}

function normalizeAllowedBaseUrl(rawUrl) {
  const raw = String(rawUrl || '').trim();
  if (!raw || /[\s\\]/.test(raw)) {
    return null;
  }
  let parsed;
  try {
    parsed = new URL(raw);
  } catch {
    return null;
  }
  if (parsed.protocol !== 'https:' || parsed.username || parsed.password || parsed.port) {
    return null;
  }
  if ((parsed.pathname && parsed.pathname !== '/') || parsed.search || parsed.hash) {
    return null;
  }
  const hostname = parsed.hostname.toLowerCase();
  if (!isAllowedWeixinHost(hostname)) {
    return null;
  }
  return `https://${hostname}`;
}

function buildRedirectBaseUrl(redirectHost) {
  const raw = String(redirectHost || '').trim();
  if (!raw || /[\s\\]/.test(raw)) {
    return null;
  }
  const candidate = raw.includes('://') ? raw : `https://${raw}`;
  return normalizeAllowedBaseUrl(candidate);
}

async function runLogin({
  client,
  state,
  onStatus = async () => {},
  fetchVerifyCode = async () => null,
  log = console,
  signal,
  shortPollMs = 5000,
  longPollMs = 40000,
  maxRefresh = 3,
  sleepImpl = (ms) => new Promise((r) => setTimeout(r, ms)),
} = {}) {
  let refreshCount = 0;
  let activeBaseUrl = undefined;
  let needVerifyCode = false;
  let lastReportedStatus = '';

  async function requestNewQr() {
    const qrRes = await client.getBotQrcode({ baseUrl: activeBaseUrl, signal });
    const qrcode = qrRes.qrcode || '';
    const qrContent = normalizeQrContent(qrRes);
    if (!qrcode || !qrContent) {
      throw new Error('iLink get_bot_qrcode missing qrcode');
    }
    needVerifyCode = false;
    lastReportedStatus = 'qr_ready';
    await onStatus({
      status: 'qr_ready',
      qr_content: qrContent,
      qr_expires_at: qrRes.expires_at || '',
    });
    return qrcode;
  }

  let qrcode = await requestNewQr();

  while (!signal || !signal.aborted) {
    let verifyCode = null;
    if (needVerifyCode) {
      verifyCode = await fetchVerifyCode().catch(() => null);
    }

    let statusRes;
    try {
      statusRes = await client.getQrcodeStatus({
        qrcode,
        verifyCode: verifyCode || undefined,
        baseUrl: activeBaseUrl,
        timeoutMs: needVerifyCode ? shortPollMs : longPollMs,
        signal,
      });
    } catch (err) {
      if (signal && signal.aborted) {
        return { ok: false, reason: 'aborted' };
      }
      if (err instanceof IlinkTimeoutError) {
        continue;
      }
      log.warn?.('[ilink-login] get_qrcode_status transient error:', err.message);
      await sleepImpl(1000);
      continue;
    }

    const st = statusRes && statusRes.status;
    if (!st || st === 'wait') {
      continue;
    }

    if (st === 'scaned' || st === 'scanned') {
      needVerifyCode = false;
      if (lastReportedStatus !== 'scanned') {
        lastReportedStatus = 'scanned';
        await onStatus({ status: 'scanned' });
      }
      continue;
    }

    if (st === 'scaned_but_redirect') {
      const redirected = buildRedirectBaseUrl(statusRes.redirect_host);
      if (redirected) {
        activeBaseUrl = redirected;
      }
      needVerifyCode = false;
      if (lastReportedStatus !== 'scanned') {
        lastReportedStatus = 'scanned';
        await onStatus({ status: 'scanned' });
      }
      continue;
    }

    if (st === 'need_verifycode' || st === 'confirmed_but_need_verifycode') {
      needVerifyCode = true;
      if (lastReportedStatus !== 'need_verifycode') {
        lastReportedStatus = 'need_verifycode';
        await onStatus({ status: 'need_verifycode' });
      }
      continue;
    }

    if (st === 'binded_redirect') {
      await onStatus({
        status: 'error',
        last_error: '该机器人已绑定其他实例，请先在原实例解绑',
      });
      return { ok: false, reason: 'already_bound' };
    }

    if (st === 'expired' || st === 'verify_code_blocked') {
      refreshCount += 1;
      if (refreshCount > maxRefresh) {
        await onStatus({
          status: 'expired',
          last_error: '二维码已多次过期，请重新登录',
        });
        return { ok: false, reason: 'expired' };
      }
      qrcode = await requestNewQr();
      continue;
    }

    if (st === 'confirmed') {
      if (!statusRes.ilink_bot_id || !statusRes.bot_token) {
        await onStatus({
          status: 'error',
          last_error: 'iLink 登录确认返回缺少 bot 信息',
        });
        return { ok: false, reason: 'invalid_confirmed' };
      }
      const validatedBaseUrl = normalizeAllowedBaseUrl(statusRes.baseurl);
      const account = {
        bot_token: statusRes.bot_token,
        ilink_bot_id: statusRes.ilink_bot_id,
        baseurl: validatedBaseUrl || activeBaseUrl || undefined,
        ilink_user_id: statusRes.ilink_user_id || '',
      };
      state.writeAccount(account);
      try {
        await onStatus({
          status: 'active',
          ilink_bot_id: account.ilink_bot_id,
          ilink_user_id: account.ilink_user_id,
        });
      } catch (err) {
        state.clearAccount();
        await onStatus({
          status: 'error',
          last_error: err.message || '激活实例冲突',
        }).catch(() => {});
        return { ok: false, reason: 'already_bound' };
      }
      return { ok: true, account };
    }
  }

  return { ok: false, reason: 'aborted' };
}

module.exports = {
  runLogin,
  buildRedirectBaseUrl,
  normalizeAllowedBaseUrl,
};
