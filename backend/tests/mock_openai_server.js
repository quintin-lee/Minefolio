// Mock OpenAI server for E2E testing of Minefolio AI Chat
const http = require('http');

const PORT = 18080;

const server = http.createServer((req, res) => {
  const url = req.url || '';
  if (req.method === 'POST' && (url.includes('/chat/completions') || url.endsWith('/chat/completions'))) {
    let body = '';
    req.on('data', chunk => { body += chunk; });
    req.on('end', () => {
      let parsed = {};
      try { parsed = JSON.parse(body); } catch {}
      const userMsg = parsed.messages?.findLast?.(m => m.role === 'user')?.content || '测试';

      res.writeHead(200, {
        'Content-Type': 'text/event-stream; charset=utf-8',
        'Cache-Control': 'no-cache',
        'Connection': 'keep-alive',
      });

      const chunks = [
        '你好！我是 **Minefolio** 智能理财助手。\n\n',
        '收到您的咨询：`' + userMsg + '`\n\n',
        '### 财务分析建议：\n',
        '1. **资产配置**：建议保持流动资金覆盖 3-6 个月生活支出。\n',
        '2. **投资组合**：多元化配置基金、债券与稳健理财。\n',
        '3. **记账习惯**：坚持记录日常收支，定期生成月度分析报表。',
      ];

      let i = 0;
      const timer = setInterval(() => {
        if (i < chunks.length) {
          const payload = {
            id: 'chatcmpl-mock-' + Date.now(),
            object: 'chat.completion.chunk',
            created: Math.floor(Date.now() / 1000),
            model: parsed.model || 'gpt-4o',
            choices: [{
              index: 0,
              delta: { content: chunks[i] },
              finish_reason: null,
            }],
          };
          res.write(`data: ${JSON.stringify(payload)}\n\n`);
          i++;
        } else {
          clearInterval(timer);
          res.write(`data: ${JSON.stringify({
            id: 'chatcmpl-mock-done',
            object: 'chat.completion.chunk',
            created: Math.floor(Date.now() / 1000),
            model: parsed.model || 'gpt-4o',
            choices: [{ index: 0, delta: {}, finish_reason: 'stop' }],
          })}\n\n`);
          res.write('data: [DONE]\n\n');
          res.end();
        }
      }, 50);
    });
  } else if (url.includes('/models') || url.endsWith('/models')) {
    res.writeHead(200, { 'Content-Type': 'application/json' });
    res.end(JSON.stringify({
      object: 'list',
      data: [{ id: 'gpt-4o', object: 'model' }, { id: 'gpt-4o-mini', object: 'model' }]
    }));
  } else {
    res.writeHead(200, { 'Content-Type': 'application/json' });
    res.end(JSON.stringify({ status: 'ok' }));
  }
});

server.listen(PORT, '127.0.0.1', () => {
  console.log(`Mock OpenAI server listening on http://127.0.0.1:${PORT}`);
});
