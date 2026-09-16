// Mock MCP streamable-HTTP server for tests/test_mcp.sh.
// Speaks JSON-RPC 2.0 over HTTP POST. Supports:
//   - initialize   -> protocolVersion + serverInfo
//   - tools/list   -> one tool "mcp_echo" with a simple schema
//   - tools/call   -> echoes the input back as a JSON text block
//
// Uses a plain node http server (no child process spawn on the Minefolio side:
// the HTTP transport is libcurl, so this mock is fully verifiable in a sandbox
// where fork+execve child stdio is unreliable — design §15⑤).
const http = require('http');

const PORT = process.env.MOCK_MCP_HTTP_PORT || 18085;

const TOOL = {
  name: 'mcp_echo',
  description: 'echo back the input object as a JSON text result',
  inputSchema: {
    type: 'object',
    properties: {
      msg: { type: 'string', description: 'text to echo' },
    },
    required: ['msg'],
  },
};

function sendJson(res, obj, code = 200) {
  const body = JSON.stringify(obj);
  res.writeHead(code, { 'Content-Type': 'application/json' });
  res.end(body);
}

const server = http.createServer((req, res) => {
  // Health probe used by the test to wait for readiness.
  if (req.method === 'GET' && req.url === '/healthz') {
    sendJson(res, { ok: true });
    return;
  }

  if (req.method !== 'POST') {
    sendJson(res, { jsonrpc: '2.0', id: null, error: { code: -32600, message: 'method not allowed' } }, 405);
    return;
  }

  let raw = '';
  req.on('data', (c) => (raw += c));
  req.on('end', () => {
    let msg;
    try {
      msg = JSON.parse(raw);
    } catch (e) {
      sendJson(res, { jsonrpc: '2.0', id: null, error: { code: -32700, message: 'parse error' } });
      return;
    }
    const id = msg.id;
    switch (msg.method) {
      case 'initialize':
        sendJson(res, {
          jsonrpc: '2.0',
          id,
          result: {
            protocolVersion: '2024-11-05',
            capabilities: { tools: {} },
            serverInfo: { name: 'mock-mcp-http', version: '1.0.0' },
          },
        });
        break;
      case 'notifications/initialized':
        sendJson(res, { jsonrpc: '2.0', result: {} });
        break;
      case 'tools/list':
        sendJson(res, { jsonrpc: '2.0', id, result: { tools: [TOOL] } });
        break;
      case 'tools/call': {
        const args = (msg.params && msg.params.arguments) || {};
        const text = JSON.stringify({ echoed: args, ok: true });
        sendJson(res, {
          jsonrpc: '2.0',
          id,
          result: { content: [{ type: 'text', text }], isError: false },
        });
        break;
      }
      default:
        sendJson(res, { jsonrpc: '2.0', id, error: { code: -32601, message: 'method not found' } });
    }
  });
});

server.listen(PORT, '127.0.0.1', () => {
  process.stdout.write(`mock-mcp-http listening on 127.0.0.1:${PORT}\n`);
});
