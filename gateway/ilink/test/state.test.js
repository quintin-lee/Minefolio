'use strict';

const { test, describe } = require('node:test');
const assert = require('node:assert');
const fs = require('fs');
const os = require('os');
const path = require('path');

const { createStateStore, removeInstanceDir, instanceDir } = require('../state');

describe('ilink/state', () => {
  test('isolates per-instance directories with 0700/0600 permissions and handles corrupt JSON', () => {
    const tmpRoot = fs.mkdtempSync(path.join(os.tmpdir(), 'mf-ilink-state-'));
    try {
      const s1 = createStateStore({ rootDir: tmpRoot, instanceId: 1 });
      const s2 = createStateStore({ rootDir: tmpRoot, instanceId: 2 });

      s1.writeAccount({ bot_token: 'tok_1', ilink_bot_id: 'bot_1' });
      s2.writeAccount({ bot_token: 'tok_2', ilink_bot_id: 'bot_2' });

      assert.strictEqual(s1.readAccount().bot_token, 'tok_1');
      assert.strictEqual(s2.readAccount().bot_token, 'tok_2');

      const dirStat = fs.statSync(instanceDir(tmpRoot, 1));
      const fileStat = fs.statSync(path.join(instanceDir(tmpRoot, 1), 'account.json'));
      assert.strictEqual(dirStat.mode & 0o777, 0o700);
      assert.strictEqual(fileStat.mode & 0o777, 0o600);

      // Corrupt JSON returns null gracefully
      fs.writeFileSync(path.join(instanceDir(tmpRoot, 1), 'cursor.json'), '{bad json', {
        mode: 0o600,
      });
      assert.strictEqual(s1.readCursor(), null);

      removeInstanceDir(tmpRoot, 1);
      assert.strictEqual(fs.existsSync(instanceDir(tmpRoot, 1)), false);
      assert.strictEqual(s2.readAccount().bot_token, 'tok_2');
    } finally {
      fs.rmSync(tmpRoot, { recursive: true, force: true });
    }
  });

  test('persists, isolates, chmods 0600, and clears context.json per instance', () => {
    const tmpRoot = fs.mkdtempSync(path.join(os.tmpdir(), 'mf-ilink-ctx-'));
    try {
      const s1 = createStateStore({ rootDir: tmpRoot, instanceId: 1 });
      const s2 = createStateStore({ rootDir: tmpRoot, instanceId: 2 });

      assert.strictEqual(s1.readContext(), null);
      s1.writeContext({
        from_user_id: 'wx_1',
        context_token: 'ctx_tok_1',
        updated_at: '2026-11-25T10:00:00Z',
        invalidated: false,
      });

      assert.strictEqual(s1.readContext().context_token, 'ctx_tok_1');
      assert.strictEqual(s2.readContext(), null);
      const stat = fs.statSync(path.join(instanceDir(tmpRoot, 1), 'context.json'));
      assert.strictEqual(stat.mode & 0o777, 0o600);

      s1.clearContext();
      assert.strictEqual(s1.readContext(), null);
    } finally {
      fs.rmSync(tmpRoot, { recursive: true, force: true });
    }
  });
});
