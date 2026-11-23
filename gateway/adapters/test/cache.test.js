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
