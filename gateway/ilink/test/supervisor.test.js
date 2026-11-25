'use strict';

const { test, describe } = require('node:test');
const assert = require('node:assert');
const EventEmitter = require('events');
const fs = require('fs');
const os = require('os');
const path = require('path');

const { createSupervisor } = require('../supervisor');
const { createStateStore, instanceDir } = require('../state');

describe('ilink/supervisor', () => {
  test('spawns, restarts on epoch change, cleans deleted state, and backs off on crash', async () => {
    const tmpRoot = fs.mkdtempSync(path.join(os.tmpdir(), 'mf-ilink-sup-'));
    try {
      const spawned = [];
      const killed = [];
      const statusCalls = [];
      let nowMs = 1_000_000;

      const sup = createSupervisor({
        api: {
          listInstances: async () => [],
          updateStatus: async (id, payload) => statusCalls.push({ id, payload }),
        },
        stateRoot: tmpRoot,
        runnerPath: '/fake/runner.js',
        nowFn: () => nowMs,
        spawnImpl: (runnerPath, args) => {
          const proc = new EventEmitter();
          proc.args = args;
          proc.kill = (sig) => killed.push({ args, sig });
          spawned.push(proc);
          return proc;
        },
        killImpl: (proc, sig) => proc.kill(sig),
      });

      // 1. Start active instance
      await sup.reconcile([{ id: 10, status: 'active', login_epoch: 0, enabled: 1 }]);
      assert.strictEqual(spawned.length, 1);
      assert.deepStrictEqual(spawned[0].args, ['--instance', '10']);

      // Write state for instance 10
      createStateStore({ rootDir: tmpRoot, instanceId: 10 }).writeAccount({ bot_token: 'secret' });

      // 2. Epoch changes -> kill and respawn with --force-login
      await sup.reconcile([{ id: 10, status: 'pending_login', login_epoch: 1, enabled: 1 }]);
      assert.strictEqual(killed.length, 1);
      assert.strictEqual(spawned.length, 2);
      assert.deepStrictEqual(spawned[1].args, ['--instance', '10', '--force-login']);

      // 3. Disabled -> stop child (preserve state)
      await sup.reconcile([{ id: 10, status: 'disabled', login_epoch: 1, enabled: 0 }]);
      assert.strictEqual(killed.length, 2);
      assert.strictEqual(fs.existsSync(instanceDir(tmpRoot, 10)), true);

      // 4. Deleted -> remove state directory
      await sup.reconcile([]);
      assert.strictEqual(fs.existsSync(instanceDir(tmpRoot, 10)), false);

      // 5. Crash backoff & 5 failures halt with error status
      for (let i = 0; i < 5; i++) {
        await sup.reconcile([{ id: 20, status: 'active', login_epoch: 0, enabled: 1 }]);
        const child = spawned[spawned.length - 1];
        child.emit('exit', 1);
        nowMs += 31_000;
      }
      assert.strictEqual(statusCalls.length, 1);
      assert.strictEqual(statusCalls[0].id, 20);
      assert.strictEqual(statusCalls[0].payload.status, 'error');

      // Does not restart while halted at same epoch
      const countBefore = spawned.length;
      await sup.reconcile([{ id: 20, status: 'active', login_epoch: 0, enabled: 1 }]);
      assert.strictEqual(spawned.length, countBefore);

      // Epoch advanced (user relogin) -> halted marker & crash history reset, runner respawns
      await sup.reconcile([{ id: 20, status: 'pending_login', login_epoch: 1, enabled: 1 }]);
      assert.strictEqual(spawned.length, countBefore + 1);
      assert.deepStrictEqual(spawned[spawned.length - 1].args, ['--instance', '20', '--force-login']);

      // A single crash on the new epoch must back off cleanly instead of immediately halting again
      spawned[spawned.length - 1].emit('exit', 1);
      assert.strictEqual(statusCalls.length, 1);
    } finally {
      fs.rmSync(tmpRoot, { recursive: true, force: true });
    }
  });
});
