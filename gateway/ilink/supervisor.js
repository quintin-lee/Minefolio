'use strict';

const path = require('path');
const { spawn } = require('child_process');
const { removeInstanceDir, listStateDirs } = require('./state');

const NEEDED_STATUSES = new Set([
  'pending_login',
  'qr_ready',
  'scanned',
  'need_verifycode',
  'active',
]);

const BACKOFF_DELAYS_MS = [2000, 4000, 8000, 16000, 30000];
const CRASH_WINDOW_MS = 5 * 60 * 1000;
const CRASH_THRESHOLD = 5;
const DEFAULT_SHUTDOWN_GRACE_MS = 3500;

function parseBoolFlag(v) {
  if (v === null || v === undefined || v === false || v === 0) return false;
  if (typeof v === 'number') return Number.isFinite(v) && v !== 0;
  if (typeof v === 'string') {
    const s = v.trim().toLowerCase();
    if (s === '' || s === '0' || s === 'false' || s === 'f') return false;
    return true;
  }
  return Boolean(v);
}

function defaultSpawn(runnerPath, args) {
  return spawn(process.execPath, [runnerPath, ...args], {
    stdio: 'inherit',
    env: process.env,
  });
}

function defaultKill(child, sig = 'SIGTERM') {
  try {
    child.kill(sig);
  } catch {
    // Ignore if already exited
  }
}

function createSupervisor({
  api,
  stateRoot,
  runnerPath = path.resolve(__dirname, 'runner.js'),
  spawnImpl = defaultSpawn,
  killImpl = defaultKill,
  maxInstances = Number(process.env.ILINK_MAX_INSTANCES || 20),
  shutdownGraceMs = DEFAULT_SHUTDOWN_GRACE_MS,
  log = console,
  nowFn = () => Date.now(),
} = {}) {
  const children = new Map(); // id -> { proc, epoch, stopping }
  const crashState = new Map(); // id -> { epoch: number, timestamps: number[], nextRetryAt: number, haltedEpoch: number | null }
  let shuttingDown = false;

  function getCrashInfo(id, epoch = 0) {
    if (!crashState.has(id)) {
      crashState.set(id, { epoch, timestamps: [], nextRetryAt: 0, haltedEpoch: null });
    }
    return crashState.get(id);
  }

  function stopChild(id, sig = 'SIGTERM') {
    const entry = children.get(id);
    if (!entry) return;
    entry.stopping = true;
    killImpl(entry.proc, sig);
    children.delete(id);
  }

  function startChild(inst, forceLogin) {
    const id = Number(inst.id);
    const epoch = Number(inst.login_epoch || 0);
    const args = ['--instance', String(id)];
    if (forceLogin) args.push('--force-login');
    const proc = spawnImpl(runnerPath, args);
    let resolveExit;
    const exitPromise = new Promise((resolve) => {
      resolveExit = resolve;
    });
    const entry = { proc, epoch, stopping: false, exited: false, exitPromise };
    children.set(id, entry);

    proc.on?.('exit', (code) => {
      entry.exited = true;
      resolveExit();
      if (children.get(id) === entry) {
        children.delete(id);
      }
      if (shuttingDown || entry.stopping) {
        return;
      }
      if (code === 0 || code === 2 || code === 4) {
        // Normal exit, session_expired (-14), or bind conflict
        return;
      }
      const now = nowFn();
      const info = getCrashInfo(id, epoch);
      info.timestamps = info.timestamps.filter((t) => now - t <= CRASH_WINDOW_MS);
      info.timestamps.push(now);
      if (info.timestamps.length >= CRASH_THRESHOLD) {
        info.haltedEpoch = epoch;
        api
          .updateStatus(id, {
            status: 'error',
            last_error: '子进程多次异常退出，已停止自动重启',
          })
          .catch(() => {});
        return;
      }
      const idx = Math.min(info.timestamps.length - 1, BACKOFF_DELAYS_MS.length - 1);
      info.nextRetryAt = now + BACKOFF_DELAYS_MS[idx];
    });
  }

  async function reconcile(instances) {
    if (shuttingDown) return;
    const list = Array.isArray(instances) ? instances : [];
    const byId = new Map(list.map((inst) => [Number(inst.id), inst]));

    for (const id of Array.from(children.keys())) {
      if (!byId.has(id)) {
        stopChild(id);
        crashState.delete(id);
        removeInstanceDir(stateRoot, id);
      }
    }

    for (const dirId of listStateDirs(stateRoot)) {
      if (!byId.has(dirId)) {
        removeInstanceDir(stateRoot, dirId);
      }
    }

    for (const inst of list) {
      const id = Number(inst.id);
      const epoch = Number(inst.login_epoch || 0);
      const enabled = parseBoolFlag(inst.enabled);
      const shouldRun = enabled && NEEDED_STATUSES.has(inst.status);

      if (!shouldRun) {
        if (children.has(id)) {
          stopChild(id);
        }
        continue;
      }

      const existing = children.get(id);
      if (existing) {
        if (existing.epoch !== epoch) {
          stopChild(id);
          crashState.delete(id);
          startChild(inst, true);
        }
        continue;
      }

      const info = getCrashInfo(id, epoch);
      if (info.epoch !== epoch || (info.haltedEpoch !== null && info.haltedEpoch !== epoch)) {
        info.epoch = epoch;
        info.haltedEpoch = null;
        info.timestamps = [];
        info.nextRetryAt = 0;
      }
      if (info.haltedEpoch !== null && info.haltedEpoch === epoch) {
        continue;
      }
      if (info.nextRetryAt > nowFn()) {
        continue;
      }

      if (children.size >= maxInstances) {
        log.warn?.(`[ilink-supervisor] maxInstances (${maxInstances}) reached, skipping ${id}`);
        continue;
      }

      startChild(inst, epoch > 0 && inst.status === 'pending_login');
    }
  }

  async function tick() {
    if (shuttingDown) return;
    const instances = await api.listInstances();
    await reconcile(instances);
  }

  async function shutdown() {
    shuttingDown = true;
    const active = Array.from(children.values());
    for (const id of Array.from(children.keys())) {
      stopChild(id, 'SIGTERM');
    }
    if (active.length > 0) {
      let timer;
      const waitExits = Promise.all(active.map((entry) => entry.exitPromise));
      const waitTimeout = new Promise((resolve) => {
        timer = setTimeout(resolve, shutdownGraceMs);
      });
      await Promise.race([waitExits, waitTimeout]);
      clearTimeout(timer);
      for (const entry of active) {
        if (!entry.exited) {
          killImpl(entry.proc, 'SIGKILL');
        }
      }
    }
  }

  return {
    children,
    reconcile,
    tick,
    shutdown,
  };
}

module.exports = {
  NEEDED_STATUSES,
  BACKOFF_DELAYS_MS,
  DEFAULT_SHUTDOWN_GRACE_MS,
  parseBoolFlag,
  createSupervisor,
};
