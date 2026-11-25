'use strict';

const { test, describe } = require('node:test');
const assert = require('node:assert');
const { formatPushText, splitPushChunks } = require('../formatter');

describe('ilink/formatter', () => {
  test('formatPushText strips Markdown noise and prepends title', () => {
    const md = [
      '### 📊 今日早报',
      '',
      '> 触发来源: **cron_tick**',
      '',
      '- **沪深300**: `+1.2%` [详情](https://example.com/a)',
      '',
      '| 标的 | 盈亏 |',
      '|---|---|',
      '| 茅台 | +500 |',
      '',
      '---',
    ].join('\n');

    const out = formatPushText('每日早报', md);
    assert.ok(out.startsWith('【每日早报】\n\n'));
    assert.ok(!out.includes('###'));
    assert.ok(!out.includes('**'));
    assert.ok(!out.includes('`'));
    assert.ok(!out.includes('|---|'));
    assert.ok(out.includes('详情 (https://example.com/a)'));
    assert.ok(out.includes('茅台 · +500'));
  });

  test('splitPushChunks breaks cleanly at newline boundaries near chunk limit', () => {
    const line = 'a'.repeat(15);
    const text = `${line}\n${line}\n${line}`;
    const chunks = splitPushChunks(text, 25);
    assert.strictEqual(chunks.length, 3);
    assert.strictEqual(chunks[0], line);
    assert.strictEqual(chunks[1], line);
    assert.strictEqual(chunks[2], line);
  });

  test('splitPushChunks never splits UTF-16 surrogate pairs across chunks', () => {
    const input = `${'A'.repeat(9)}📊${'B'.repeat(9)}`;
    const chunks = splitPushChunks(input, 10);
    assert.strictEqual(chunks.join(''), input);
    for (const c of chunks) {
      assert.doesNotThrow(() => encodeURIComponent(c));
    }

    const tiny = splitPushChunks('📊A', 1);
    assert.deepStrictEqual(tiny, ['📊', 'A']);
  });

  test('formatPushText deduplicates title and splitPushChunks handles empty/whitespace and hard cuts', () => {
    assert.strictEqual(formatPushText('每日早报', '【每日早报】\n\n正文'), '【每日早报】\n\n正文');
    assert.strictEqual(formatPushText('每日早报', '每日早报\n正文'), '每日早报\n正文');
    assert.strictEqual(formatPushText('每日早报', ''), '【每日早报】');

    assert.deepStrictEqual(splitPushChunks(''), []);
    assert.deepStrictEqual(splitPushChunks('   \n\t  '), []);
    assert.deepStrictEqual(splitPushChunks('abcdefghij', 4), ['abcd', 'efgh', 'ij']);
  });
});
