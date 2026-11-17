#!/usr/bin/env node
/**
 * CLI simulating an IM user — runs the gateway core against a live backend.
 *
 * Usage:
 *   node cli.js <platform> <imUserId> <bindToken> "<message>"
 *   node cli.js qq 12345 my_bind_token "我最近一个月花了多少"
 *   node cli.js qq 12345 my_bind_token "/bind <绑定码>"   # bind flow
 *
 * Platform adapters (NapCat/OneBot, wechaty) call the same handleImMessage()
 * with the same arguments; this CLI is the minimal harness that proves the
 * read-only query loop end-to-end without a real IM runtime.
 */

const { handleImMessage } = require('./index.js');

async function main() {
  const [, , platform, imUserId, bindToken, ...rest] = process.argv;
  if (!platform || !imUserId || !rest || rest.length === 0) {
    console.error('Usage: node cli.js <platform> <imUserId> <bindToken> "<message>"');
    console.error('  platform: qq | wechat');
    console.error('  bindToken: a valid im_bind token, or "none" when not yet bound');
    console.error('  /bind <code> performs a bind instead of a query');
    process.exit(2);
  }
  const text = rest.join(' ');
  const reply = await handleImMessage(platform, imUserId, text, bindToken);
  console.log(reply);
}

main().catch((e) => {
  console.error('gateway error:', e && e.message ? e.message : e);
  process.exit(1);
});
