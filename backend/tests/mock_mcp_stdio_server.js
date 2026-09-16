// Mock MCP stdio server for tests/test_mcp.sh.
// Speaks JSON-RPC 2.0 over stdio (newline-delimited). Supports:
//   - initialize   -> protocolVersion + serverInfo
//   - tools/list   -> one tool "mcp_echo" with a simple schema
//   - tools/call   -> echoes the input back as a JSON text block
//
// The real Minefolio backend bridges to this server via
// services/ai/tools/mcp/mcp_client.c (posix_spawn transport). This script
// proves the round-trip without needing a real remote MCP deployment.
const readline = require('readline');

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

function send(obj) {
  process.stdout.write(JSON.stringify(obj) + '\n');
}

const rl = readline.createInterface({ input: process.stdin, terminal: false });

rl.on('line', (line) => {
  let msg;
  try { msg = JSON.parse(line); } catch (e) {
    // 非 JSON 行：按 protocol 规范回 parse error，帧 id 未知用 null。
    send({ jsonrpc: '2.0', id: null, error: { code: -32700, message: 'parse error' } });
    return;
  }
  const id = msg.id;
  switch (msg.method) {
    case 'initialize':
      send({
        jsonrpc: '2.0', id,
        result: {
          protocolVersion: '2024-11-05',
          capabilities: { tools: {} },
          serverInfo: { name: 'mock-mcp', version: '1.0.0' },
        },
      });
      break;
    case 'notifications/initialized':
      // 通知：无响应（按 MCP 规范）。
      break;
    case 'tools/list':
      send({ jsonrpc: '2.0', id, result: { tools: [TOOL] } });
      break;
    case 'tools/call': {
      const args = (msg.params && msg.params.arguments) || {};
      const text = JSON.stringify({ echoed: args, ok: true });
      send({
        jsonrpc: '2.0', id,
        result: { content: [{ type: 'text', text }], isError: false },
      });
      break;
    }
    default:
      send({ jsonrpc: '2.0', id, error: { code: -32601, message: 'method not found' } });
  }
});
