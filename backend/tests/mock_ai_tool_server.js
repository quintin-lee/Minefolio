// Mock OpenAI-compatible server for the AI tool-call regression test
// (tests/test_ai_tool_call.sh).
//
// Streaming behaviour replicates the wire format that the csilk OpenAI driver
// parses (see csilk tests/drivers/openai_mock_server.py), verified against the
// real Minefolio backend:
//   - turn 1 (no role="tool" message in history): stream TOOL_COUNT parallel
//     tool calls named after the comma-separated TOOL_NAMES env list
//     (default: get_summary — a tool registered by the Minefolio backend);
//   - later turns (history contains role="tool" messages): stream a plain text
//     answer so the agent loop terminates with "done".
//
// Env: MOCK_TOOL_COUNT (default 1), MOCK_TOOL_NAMES (comma separated,
// default "get_summary"), MOCK_PORT (default 18080).
const http = require('http');

const PORT = Number(process.env.MOCK_PORT || 18080);
const TOOL_COUNT = Number(process.env.MOCK_TOOL_COUNT || 1);
const TOOL_NAMES = (process.env.MOCK_TOOL_NAMES || 'get_summary').split(',').map(s => s.trim());

function chunk(res, choices, extra) {
  const payload = Object.assign({
    id: 'chatcmpl-mock-' + Date.now(),
    object: 'chat.completion.chunk',
    created: Math.floor(Date.now() / 1000),
    model: 'mock-model',
  }, extra || {}, { choices });
  res.write(`data: ${JSON.stringify(payload)}\n\n`);
}

function streamToolCallTurn(res) {
  // 1. one meta chunk per tool: role + index/id/type/function name
  for (let i = 0; i < TOOL_COUNT; i++) {
    chunk(res, [{
      index: 0,
      delta: {
        role: 'assistant',
        tool_calls: [{
          index: i,
          id: 'call_mock_00' + i,
          type: 'function',
          function: { name: TOOL_NAMES[i] || TOOL_NAMES[0], arguments: '' },
        }],
      },
      finish_reason: null,
    }]);
  }
  // 2. one arguments chunk per tool; finish_reason marks the end of the turn
  for (let i = 0; i < TOOL_COUNT; i++) {
    const args = TOOL_NAMES[i] === 'get_assets' ? '{"page": 1, "page_size": 20}' : '{}';
    chunk(res, [{
      index: 0,
      delta: { tool_calls: [{ index: i, function: { arguments: args } }] },
      finish_reason: i === TOOL_COUNT - 1 ? 'tool_calls' : null,
    }]);
  }
  chunk(res, [], {
    usage: { prompt_tokens: 15, completion_tokens: 5, total_tokens: 20 },
  });
  res.write('data: [DONE]\n\n');
  res.end();
}

function streamTextTurn(res) {
  const parts = ['查询完成', '，以上是通过工具获取的最新数据汇总。'];
  for (let i = 0; i < parts.length; i++) {
    chunk(res, [{
      index: 0,
      delta: i === 0
        ? { role: 'assistant', content: parts[i] }
        : { content: parts[i] },
      finish_reason: i === parts.length - 1 ? 'stop' : null,
    }]);
  }
  chunk(res, [], {
    usage: { prompt_tokens: 30, completion_tokens: 12, total_tokens: 42 },
  });
  res.write('data: [DONE]\n\n');
  res.end();
}

const server = http.createServer((req, res) => {
  if (req.method === 'POST' && (req.url || '').includes('/chat/completions')) {
    let body = '';
    req.on('data', c => { body += c; });
    req.on('end', () => {
      let parsed = {};
      try { parsed = JSON.parse(body); } catch (e) { /* ignore */ }
      const messages = parsed.messages || [];
      const hasToolResult = messages.some(m => m && m.role === 'tool');
      res.writeHead(200, {
        'Content-Type': 'text/event-stream',
        'Cache-Control': 'no-cache',
        'Connection': 'close',
      });
      if (hasToolResult) {
        streamTextTurn(res);
      } else {
        streamToolCallTurn(res);
      }
    });
    return;
  }
  if ((req.url || '').includes('/models')) {
    res.writeHead(200, { 'Content-Type': 'application/json' });
    res.end(JSON.stringify({ object: 'list', data: [{ id: 'mock-model' }] }));
    return;
  }
  res.writeHead(404, { 'Content-Type': 'application/json' });
  res.end(JSON.stringify({ error: 'not found' }));
});

server.listen(PORT, '127.0.0.1', () => {
  console.log(`Mock AI tool server listening on http://127.0.0.1:${PORT} (tools=${TOOL_COUNT}: ${TOOL_NAMES.join(',')})`);
});
