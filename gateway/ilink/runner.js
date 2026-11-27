'use strict';

const path = require('path');
const { createGatewayApi, GatewayHttpError } = require('./api');
const { createIlinkClient, IlinkSessionExpiredError } = require('./client');
const { createStateStore } = require('./state');
const { runLogin } = require('./login');
const { runMonitor } = require('./monitor');
const { createPusher } = require('./pusher');
const { parseBoolFlag } = require('./supervisor');

function isInstanceEnabled(inst) {
  return Boolean(inst && parseBoolFlag(inst.enabled));
}

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

function pickSessionError(results, workerErrors) {
  const errors = [];
  for (const r of results || []) {
    if (r && r.status === 'rejected' && r.reason) {
      errors.push(r.reason);
    }
  }
  for (const err of workerErrors || []) {
    if (err && !errors.includes(err)) {
      errors.push(err);
    }
  }
  if (errors.length === 0) return null;
  const expired = errors.find((e) => e instanceof IlinkSessionExpiredError);
  return expired || errors[0];
}

async function runManagedSession({
  client,
  state,
  account,
  instanceId,
  api,
  stopSignal,
  graceMs = 3000,
  onContextUpdated = () => {},
  monitorOpts = {},
  pusherOpts = {},
  log = console,
} = {}) {
  const workerController = new AbortController();
  const isExternalStop = () => Boolean(stopSignal && stopSignal.aborted);

  const onStop = () => {
    workerController.abort();
  };
  if (stopSignal) {
    if (stopSignal.aborted) {
      workerController.abort();
    } else {
      stopSignal.addEventListener('abort', onStop, { once: true });
    }
  }

  const pusher = createPusher({
    client,
    state,
    account,
    instanceId,
    api,
    log,
    signal: workerController.signal,
    ...pusherOpts,
  });

  const workerErrors = [];
  const runWithAbort = (p) =>
    Promise.resolve(p).catch((err) => {
      workerErrors.push(err);
      workerController.abort();
      throw err;
    });

  const settledPromise = Promise.allSettled([
    runWithAbort(
      runMonitor({
        client,
        state,
        account,
        instanceId,
        api,
        log,
        signal: workerController.signal,
        onContextUpdated: (ev) => {
          onContextUpdated(ev);
          pusher.wake();
        },
        ...monitorOpts,
      })
    ),
    runWithAbort(pusher.run()),
  ]);

  let graceTimer = null;
  let onWorkerAbort = null;
  let allSettled = false;

  const gracePromise = new Promise((resolve) => {
    const startGrace = () => {
      if (allSettled || graceTimer) return;
      const parsed = Number(graceMs);
      const ms = Number.isFinite(parsed) && parsed >= 0 ? parsed : 3000;
      graceTimer = setTimeout(() => resolve({ timedOut: true, results: null }), ms);
      // Keep the grace timer referenced: if worker timers are also unref'd,
      // Node can exit the event loop while runManagedSession's promise is
      // still pending ("Promise resolution is still pending"), which makes
      // shutdown tests fail intermittently on CI runners.
      // graceTimer.unref?.();
    };
    if (workerController.signal.aborted) {
      startGrace();
    } else {
      onWorkerAbort = startGrace;
      workerController.signal.addEventListener('abort', onWorkerAbort, { once: true });
    }
  });

  let outcome;
  try {
    outcome = await Promise.race([
      settledPromise.then((results) => {
        allSettled = true;
        return { timedOut: false, results };
      }),
      gracePromise,
    ]);
  } finally {
    allSettled = true;
    if (graceTimer) {
      clearTimeout(graceTimer);
      graceTimer = null;
    }
    if (onWorkerAbort) {
      workerController.signal.removeEventListener('abort', onWorkerAbort);
      onWorkerAbort = null;
    }
    if (stopSignal) {
      stopSignal.removeEventListener('abort', onStop);
    }
  }

  const fatalErr = pickSessionError(outcome && outcome.results, workerErrors);
  if (fatalErr) {
    if (fatalErr instanceof IlinkSessionExpiredError) {
      return { exitCode: 2, error: fatalErr };
    }
    if (isExternalStop()) {
      return { exitCode: 0, error: null };
    }
    log.error?.('[ilink-runner] fatal:', fatalErr.message);
    return { exitCode: 1, error: fatalErr };
  }
  return { exitCode: 0, error: null };
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

  if (!isInstanceEnabled(instance) || instance.status === 'disabled') {
    process.exit(0);
  }

  const initialEpoch = Number(instance.login_epoch || 0);
  const controller = new AbortController();
  let stopRequested = false;
  const requestStop = () => {
    stopRequested = true;
    controller.abort();
  };
  process.on('SIGTERM', requestStop);
  process.on('SIGINT', requestStop);

  let latestInboundAt = (state.readContext() && state.readContext().updated_at) || '';

  const hbTimer = setInterval(async () => {
    try {
      const hb = await api.heartbeat(instanceId, {
        status: instance.status,
        last_inbound_at: latestInboundAt || undefined,
      });
      if (
        !isInstanceEnabled(hb) ||
        hb.status === 'disabled' ||
        hb.status === 'session_expired' ||
        Number(hb.login_epoch || 0) !== initialEpoch
      ) {
        requestStop();
      }
    } catch (err) {
      if (err instanceof GatewayHttpError && (err.code === 1003 || err.code === 1004)) {
        requestStop();
      }
    }
  }, 30000);
  hbTimer.unref?.();

  let exitCode = 0;
  try {
    let account = forceLogin ? null : state.readAccount();
    if (!account || !account.bot_token) {
      if (forceLogin) {
        state.clearContext();
        latestInboundAt = '';
      }
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
        exitCode = loginRes.reason === 'already_bound' ? 4 : 0;
      } else {
        account = loginRes.account;
      }
    }

    if (account && account.bot_token) {
      const existingCtx = state.readContext();
      if (
        existingCtx &&
        account.ilink_user_id &&
        existingCtx.from_user_id &&
        existingCtx.from_user_id !== account.ilink_user_id
      ) {
        state.clearContext();
        latestInboundAt = '';
      }

      const res = await runManagedSession({
        client,
        state,
        account,
        instanceId,
        api,
        stopSignal: controller.signal,
        onContextUpdated: ({ updatedAt }) => {
          latestInboundAt = updatedAt;
        },
      });
      exitCode = res.exitCode;
    }
  } catch (err) {
    if (err instanceof IlinkSessionExpiredError) {
      exitCode = 2;
    } else if (stopRequested) {
      exitCode = 0;
    } else {
      console.error('[ilink-runner] fatal:', err.message);
      exitCode = 1;
    }
  } finally {
    clearInterval(hbTimer);
    process.off('SIGTERM', requestStop);
    process.off('SIGINT', requestStop);
  }
  process.exit(exitCode);
}

if (require.main === module) {
  main();
}

module.exports = {
  parseArgs,
  parseBoolFlag,
  isInstanceEnabled,
  pickSessionError,
  runManagedSession,
};
