'use strict';

const path = require('path');
const { createGatewayApi, GatewayHttpError } = require('./api');
const { createIlinkClient, IlinkSessionExpiredError } = require('./client');
const { createStateStore } = require('./state');
const { runLogin } = require('./login');
const { runMonitor } = require('./monitor');

function parseArgs(argv) {
  let instanceId = null;
  let forceLogin = false;
  for (let i = 0; i < argv.length; i++) {
    if (argv[i] === '--instance' && i + 1 < argv.length) {
      instanceId = argv[++i];
    } else if (argv[i] === '--force-login') {
      forceLogin = true;
    }
  }
  return { instanceId, forceLogin };
}

async function main() {
  const { instanceId, forceLogin } = parseArgs(process.argv.slice(2));
  const gatewayToken = process.env.MINEFOLIO_GATEWAY_TOKEN || '';
  if (!gatewayToken || !instanceId) {
    console.error('[ilink-runner] missing MINEFOLIO_GATEWAY_TOKEN or --instance');
    process.exit(3);
  }

  const stateRoot = process.env.ILINK_STATE_DIR || path.resolve(__dirname, '../.ilink-state');
  const api = createGatewayApi({ gatewayToken });
  const client = createIlinkClient();
  const state = createStateStore({ rootDir: stateRoot, instanceId });

  let instance;
  try {
    instance = await api.getInstance(instanceId);
  } catch (err) {
    if (err instanceof GatewayHttpError && err.code === 1001) {
      console.error('[ilink-runner] gateway auth failed');
      process.exit(3);
    }
    if (err instanceof GatewayHttpError && err.code === 1003) {
      process.exit(0);
    }
    console.error('[ilink-runner] failed to load instance:', err.message);
    process.exit(1);
  }

  if (!instance || !instance.enabled || instance.status === 'disabled') {
    process.exit(0);
  }

  const initialEpoch = Number(instance.login_epoch || 0);
  const controller = new AbortController();
  const stop = () => controller.abort();
  process.on('SIGTERM', stop);
  process.on('SIGINT', stop);

  const hbTimer = setInterval(async () => {
    try {
      const hb = await api.heartbeat(instanceId, { status: instance.status });
      if (!hb || !hb.enabled || Number(hb.login_epoch || 0) !== initialEpoch) {
        controller.abort();
      }
    } catch (err) {
      if (err instanceof GatewayHttpError && (err.code === 1003 || err.code === 1004)) {
        controller.abort();
      }
    }
  }, 30000);
  hbTimer.unref?.();

  try {
    let account = forceLogin ? null : state.readAccount();
    if (!account || !account.bot_token) {
      const loginRes = await runLogin({
        client,
        state,
        signal: controller.signal,
        onStatus: (payload) => api.updateStatus(instanceId, payload),
        fetchVerifyCode: async () => {
          const fresh = await api.getInstance(instanceId);
          return fresh && fresh.verify_code ? fresh.verify_code : null;
        },
      });
      if (!loginRes.ok) {
        if (loginRes.reason === 'already_bound') {
          process.exit(4);
        }
        process.exit(0);
      }
      account = loginRes.account;
    }

    await runMonitor({
      client,
      state,
      account,
      instanceId,
      api,
      signal: controller.signal,
    });
    process.exit(0);
  } catch (err) {
    if (err instanceof IlinkSessionExpiredError) {
      process.exit(2);
    }
    if (controller.signal.aborted) {
      process.exit(0);
    }
    console.error('[ilink-runner] fatal:', err.message);
    process.exit(1);
  } finally {
    clearInterval(hbTimer);
  }
}

if (require.main === module) {
  main();
}

module.exports = {
  parseArgs,
};
