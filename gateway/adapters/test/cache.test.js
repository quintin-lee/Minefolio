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
  gw.TOKEN_CACHE.set('qq:10001', 'token_qq_10001');
  gw.saveCache();

  assert.strictEqual(fs.existsSync(testCacheFile), true);
  const content = JSON.parse(fs.readFileSync(testCacheFile, 'utf8'));
  assert.strictEqual(content['qq:10001'], 'token_qq_10001');

  // Clear memory cache and reload
  gw.TOKEN_CACHE.clear();
  assert.strictEqual(gw.TOKEN_CACHE.has('qq:10001'), false);
  gw.loadCache();
  assert.strictEqual(gw.TOKEN_CACHE.get('qq:10001'), 'token_qq_10001');

  // Clean up
  try {
    fs.rmSync(tmpDir, { recursive: true, force: true });
  } catch {}
});
