'use strict';

const { test, describe } = require('node:test');
const assert = require('node:assert');
const EventEmitter = require('events');
const fs = require('fs');
const os = require('os');
const path = require('path');

const supervisorMod = require('../ilink/supervisor');
const runnerMod = require('../ilink/runner');

describe('ilink boolean flag parsing (SQLite/PostgreSQL defensive handling)', () => {
  test('parseBoolFlag treats 0, "0", "false", "f", "", null, undefined as false', () => {
    assert.strictEqual(typeof supervisorMod.parseBoolFlag, 'function');
    assert.strictEqual(typeof runnerMod.parseBoolFlag, 'function');

    const falsyValues = [false, 0, '0', 'false', 'FALSE', 'False', 'f', 'F', '', '   ', null, undefined];
    for (const val of falsyValues) {
      assert.strictEqual(
        supervisorMod.parseBoolFlag(val),
        false,
        `supervisor.parseBoolFlag(${JSON.stringify(val)}) should be false`
      );
      assert.strictEqual(
        runnerMod.parseBoolFlag(val),
        false,
        `runner.parseBoolFlag(${JSON.stringify(val)}) should be false`
      );
    }

    const truthyValues = [true, 1, '1', 'true', 'TRUE', 't', 'T'];
    for (const val of truthyValues) {
      assert.strictEqual(
        supervisorMod.parseBoolFlag(val),
        true,
        `supervisor.parseBoolFlag(${JSON.stringify(val)}) should be true`
      );
      assert.strictEqual(
        runnerMod.parseBoolFlag(val),
        true,
        `runner.parseBoolFlag(${JSON.stringify(val)}) should be true`
      );
    }
  });

  test('supervisor.reconcile does not spawn and stops running child on "0"/"f"/"false" enabled flags', async () => {
    const tmpRoot = fs.mkdtempSync(path.join(os.tmpdir(), 'mf-ilink-bool-'));
    try {
      const spawned = [];
      const killed = [];
      const sup = supervisorMod.createSupervisor({
        api: {
          listInstances: async () => [],
          updateStatus: async () => {},
        },
        stateRoot: tmpRoot,
        runnerPath: '/fake/runner.js',
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

      const disabledFlags = ['0', 'f', 'F', 'false', 'FALSE', '', null, undefined, 0, false];
      for (let i = 0; i < disabledFlags.length; i++) {
        await sup.reconcile([
          { id: 100 + i, status: 'active', login_epoch: 0, enabled: disabledFlags[i] },
        ]);
      }
      assert.strictEqual(spawned.length, 0, 'disabled flags must never spawn runners');

      // Truthy flag spawns
      await sup.reconcile([{ id: 200, status: 'active', login_epoch: 0, enabled: 't' }]);
      assert.strictEqual(spawned.length, 1);

      // Transition to string "f" / "0" / "false" stops running child
      await sup.reconcile([{ id: 200, status: 'active', login_epoch: 0, enabled: 'f' }]);
      assert.strictEqual(killed.length, 1);
      assert.strictEqual(sup.children.size, 0);
    } finally {
      fs.rmSync(tmpRoot, { recursive: true, force: true });
    }
  });

  test('runner.isInstanceEnabled treats "0"/"f"/"false" as disabled', () => {
    assert.strictEqual(typeof runnerMod.isInstanceEnabled, 'function');
    assert.strictEqual(runnerMod.isInstanceEnabled(null), false);
    assert.strictEqual(runnerMod.isInstanceEnabled({ enabled: '0' }), false);
    assert.strictEqual(runnerMod.isInstanceEnabled({ enabled: 'f' }), false);
    assert.strictEqual(runnerMod.isInstanceEnabled({ enabled: 'false' }), false);
    assert.strictEqual(runnerMod.isInstanceEnabled({ enabled: 0 }), false);
    assert.strictEqual(runnerMod.isInstanceEnabled({ enabled: '' }), false);
    assert.strictEqual(runnerMod.isInstanceEnabled({ enabled: '1' }), true);
    assert.strictEqual(runnerMod.isInstanceEnabled({ enabled: 't' }), true);
    assert.strictEqual(runnerMod.isInstanceEnabled({ enabled: true }), true);
  });
});
