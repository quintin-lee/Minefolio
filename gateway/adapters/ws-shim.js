/**
 * WebSocket client shim: prefers Node's global WebSocket (Node >= 21 /
 * 22-stable), falls back to the optional 'ws' package.
 * Adapts WHATWG WebSocket to EventEmitter-like .on(event, handler) API.
 */

const NativeWebSocket = typeof globalThis.WebSocket !== 'undefined' ? globalThis.WebSocket : null;

function WebSocket(url, protocols) {
  let ws;
  if (NativeWebSocket) {
    ws = new NativeWebSocket(url, protocols);
  } else {
    const Ws = require('ws');
    ws = new Ws(url, protocols);
  }

  if (typeof ws.on !== 'function') {
    ws.on = function (event, handler) {
      if (event === 'message') {
        ws.addEventListener('message', (ev) => handler(ev.data));
      } else {
        ws.addEventListener(event, handler);
      }
      return ws;
    };
  }

  return ws;
}

module.exports = { WebSocket };
