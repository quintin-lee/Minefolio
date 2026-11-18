/**
 * WebSocket client shim: prefers Node's global WebSocket (Node >= 21 /
 * 22-stable), falls back to the optional 'ws' package. Keeps the gateway
 * dependency-free unless you run against a real OneBot WS endpoint on an
 * older Node, in which case `npm i ws` in this directory.
 */

if (typeof globalThis.WebSocket !== 'undefined') {
  module.exports = { WebSocket: globalThis.WebSocket };
} else {
  module.exports = { WebSocket: require('ws') };
}
