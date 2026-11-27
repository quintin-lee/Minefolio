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
          proc.kill = (sig) => {
            killed.push({ args, sig });
            proc.emit('exit', 0);
          };
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

  test('awaits child exit before respawning on epoch change or deleting instance dir', async () => {
    const tmpRoot = fs.mkdtempSync(path.join(os.tmpdir(), 'mf-ilink-sup-wait-'));
    try {
      const events = [];
      let childSeq = 0;

      const sup = createSupervisor({
        api: {
          listInstances: async () => [],
          updateStatus: async () => {},
        },
        stateRoot: tmpRoot,
        runnerPath: '/fake/runner.js',
        shutdownGraceMs: 60,
        spawnImpl: (runnerPath, args) => {
          const seq = ++childSeq;
          events.push(`spawn:${seq}`);
          const proc = new EventEmitter();
          proc.args = args;
          proc.seq = seq;
          proc.kill = (sig) => {
            events.push(`kill:${seq}:${sig}`);
            if (seq === 1 && sig === 'SIGTERM') {
              // Simulate slow graceful exit (30ms)
              setTimeout(() => {
                events.push(`exit:${seq}`);
                proc.emit('exit', 0);
              }, 30);
            } else if (seq === 2 && sig === 'SIGKILL') {
              // Ignore SIGTERM and exit only on escalated SIGKILL
              setTimeout(() => {
                events.push(`exit:${seq}`);
                proc.emit('exit', null);
              }, 10);
            }
          };
          return proc;
        },
        killImpl: (proc, sig) => proc.kill(sig),
      });

      await sup.reconcile([{ id: 30, status: 'active', login_epoch: 0, enabled: 1 }]);
      createStateStore({ rootDir: tmpRoot, instanceId: 30 }).writeAccount({ bot_token: 'tok' });

      // Epoch change: replacement spawn:2 must occur AFTER delayed exit:1
      await sup.reconcile([{ id: 30, status: 'pending_login', login_epoch: 1, enabled: 1 }]);
      const exit1Idx = events.indexOf('exit:1');
      const spawn2Idx = events.indexOf('spawn:2');
      assert.ok(exit1Idx >= 0 && spawn2Idx > exit1Idx, `expected exit:1 before spawn:2, got ${events.join(',')}`);

      // Removal with unresponsive SIGTERM -> escalates to SIGKILL and waits for exit:2 before dir removal
      await sup.reconcile([]);
      assert.ok(events.includes('kill:2:SIGTERM'));
      assert.ok(events.includes('kill:2:SIGKILL'));
      assert.ok(events.includes('exit:2'));
      assert.strictEqual(fs.existsSync(instanceDir(tmpRoot, 30)), false);
    } finally {
      fs.rmSync(tmpRoot, { recursive: true, force: true });
    }
  });
});
