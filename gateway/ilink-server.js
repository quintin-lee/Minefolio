'use strict';

const path = require('path');
const { createGatewayApi } = require('./ilink/api');
const { createSupervisor } = require('./ilink/supervisor');

const gatewayToken = process.env.MINEFOLIO_GATEWAY_TOKEN || '';
if (!gatewayToken) {
  console.error('[ilink-server] ERROR: MINEFOLIO_GATEWAY_TOKEN is required.');
  process.exit(1);
}

const apiBase = process.env.MINEFOLIO_API || 'http://localhost:8080';
const stateRoot = process.env.ILINK_STATE_DIR || path.resolve(__dirname, '.ilink-state');
const pollMs = Number(process.env.ILINK_SUPERVISOR_POLL_MS || 5000);
const maxInstances = Number(process.env.ILINK_MAX_INSTANCES || 20);

const api = createGatewayApi({ apiBase, gatewayToken });
const supervisor = createSupervisor({ api, stateRoot, maxInstances });

let timer = null;
let ticking = false;

async function pollOnce() {
  if (ticking) return;
  ticking = true;
  try {
    await supervisor.tick();
  } catch (err) {
    console.warn('[ilink-server] reconcile failed:', err.message);
  } finally {
    ticking = false;
  }
}

console.log(`[ilink-server] starting (api=${apiBase}, state=${stateRoot}, poll=${pollMs}ms)`);
pollOnce();
timer = setInterval(pollOnce, pollMs);

async function handleExit() {
  if (timer) {
    clearInterval(timer);
    timer = null;
  }
  await supervisor.shutdown();
  process.exit(0);
}

process.on('SIGTERM', handleExit);
process.on('SIGINT', handleExit);
