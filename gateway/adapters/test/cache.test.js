const { test, before, after } = require('node:test');
const assert = require('node:assert');
const fs = require('node:fs');
const path = require('node:path');
const os = require('node:os');
const http = require('node:http');

test('IM bindings cache persists to disk and reloads properly', async () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), 'im-cache-test-'));
  const testCacheFile = path.join(tmpDir, 'test_bindings.json');
  process.env.MINEFOLIO_IM_CACHE_FILE = testCacheFile;

  // Require fresh gateway
  delete require.cache[require.resolve('../../index.js')];
  const gw = require('../../index.js');

  gw.TOKEN_CACHE.clear();
  gw.setCachedToken('qq:10001', 'token_qq_10001');

  assert.strictEqual(fs.existsSync(testCacheFile), true);
  const content = JSON.parse(fs.readFileSync(testCacheFile, 'utf8'));
  const savedVal = typeof content['qq:10001'] === 'object' ? content['qq:10001'].token : content['qq:10001'];
  assert.strictEqual(savedVal, 'token_qq_10001');

  // Clear memory cache and reload
  gw.TOKEN_CACHE.clear();
  assert.strictEqual(gw.TOKEN_CACHE.has('qq:10001'), false);
  gw.loadCache();
  assert.strictEqual(gw.getCachedToken('qq:10001'), 'token_qq_10001');

  // Also test loading legacy plain string format
  fs.writeFileSync(testCacheFile, JSON.stringify({ 'wechat:user2': 'token_legacy_123' }), 'utf8');
  gw.TOKEN_CACHE.clear();
  gw.loadCache();
  assert.strictEqual(gw.getCachedToken('wechat:user2'), 'token_legacy_123');

  // Clean up
  try {
    fs.rmSync(tmpDir, { recursive: true, force: true });
  } catch {}
});

test('IM bindings cache writes atomically with 0600 mode without altering parent dir mode', async () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), 'im-cache-perm-'));
  fs.chmodSync(tmpDir, 0o755);
  const testCacheFile = path.join(tmpDir, 'bindings.json');
  process.env.MINEFOLIO_IM_CACHE_FILE = testCacheFile;

  delete require.cache[require.resolve('../../index.js')];
  const gw = require('../../index.js');

  gw.TOKEN_CACHE.clear();
  await Promise.all(
    Array.from({ length: 10 }, (_, i) =>
      Promise.resolve().then(() => gw.setCachedToken(`qq:u${i}`, `secret_${i}`))
    )
  );

  const fileMode = fs.statSync(testCacheFile).mode & 0o777;
  const dirMode = fs.statSync(tmpDir).mode & 0o777;
  assert.strictEqual(fileMode, 0o600);
  assert.strictEqual(dirMode, 0o755);

  const leftovers = fs.readdirSync(tmpDir).filter((f) => f.includes('.tmp.'));
  assert.deepStrictEqual(leftovers, []);

  // Existing loose 0644 cache file gets tightened on loadCache()
  fs.chmodSync(testCacheFile, 0o644);
  gw.TOKEN_CACHE.clear();
  gw.loadCache();
  assert.strictEqual(fs.statSync(testCacheFile).mode & 0o777, 0o600);

  fs.rmSync(tmpDir, { recursive: true, force: true });
});
