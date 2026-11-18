/**
 * wechaty adapter guard test: without the optional `wechaty` package installed
 * the factory must throw a clear install hint (and not crash silently).
 * Does NOT start a real bot.
 */

const { test } = require('node:test');
const assert = require('node:assert');
const path = require('node:path');

test('createWechatyBot throws an install hint when wechaty is absent', async () => {
  // Skip if the optional dep happens to be installed in this environment.
  let installed = true;
  try {
    require.resolve('wechaty', { paths: [path.join(__dirname, '../../..')] });
  } catch {
    installed = false;
  }
  if (installed) {
    console.log('wechaty present, skipping absence-guard test');
    return;
  }
  const { createWechatyBot } = require('../wechaty.js');
  await assert.rejects(
    () => createWechatyBot({ getBindToken: async () => '' }),
    /wechaty is not installed/,
  );
});
