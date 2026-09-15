// Non-streaming mock OpenAI server for E2E testing of the AI summary worker.
// The summary worker (services/ai/memory/summary.c) calls csilk_ai_chat with
// stream=false, so this mock returns a plain JSON chat completion instead of
// SSE chunks. Listening on 127.0.0.1:18081.
const http = require('http');

const PORT = 18081;

const server = http.createServer((req, res) => {
  const url = req.url || '';
  if (req.method === 'POST' && url.includes('/chat/completions')) {
    let body = '';
    req.on('data', (chunk) => { body += chunk; });
    req.on('end', () => {
      let parsed = {};
      try { parsed = JSON.parse(body); } catch (_) {}
      const userMsg =
        (parsed.messages || [])
          .filter((m) => m.role === 'user')
          .map((m) => m.content)
          .join('\n')
          .slice(0, 200);

      res.writeHead(200, { 'Content-Type': 'application/json' });
      res.end(
        JSON.stringify({
          id: 'chatcmpl-summary-mock',
          object: 'chat.completion',
          created: Math.floor(Date.now() / 1000),
          model: parsed.model || 'mock-model',
          choices: [
            {
              index: 0,
              message: {
                role: 'assistant',
                content:
                  '对话摘要：用户偏好稳健型理财，重点关注现金流与资产配比；' +
                  '助手已记录关键财务事实与建议。',
              },
              finish_reason: 'stop',
            },
          ],
          usage: { prompt_tokens: 100, completion_tokens: 30, total_tokens: 130 },
        }),
      );
    });
  } else if (url.includes('/models')) {
    res.writeHead(200, { 'Content-Type': 'application/json' });
    res.end(
      JSON.stringify({
        object: 'list',
        data: [{ id: 'mock-model', object: 'model' }],
      }),
    );
  } else {
    res.writeHead(200, { 'Content-Type': 'application/json' });
    res.end(JSON.stringify({ status: 'ok' }));
  }
});

server.listen(PORT, '127.0.0.1', () => {
  console.log(`Mock summary server (non-streaming) on http://127.0.0.1:${PORT}`);
});
