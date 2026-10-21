# AI Web Search & Deep Reading Tools Design

## 1. Context & Motivation

Minefolio's AI Assistant currently possesses comprehensive capabilities for querying and managing local personal finances, portfolios, cashflows, and transactions. However, when users inquire about:
- **Financial Market Information**: Latest quarterly earnings (e.g. NVIDIA, Apple), stock news, IPOs, market trends.
- **Macroeconomic Data**: Central bank rate decisions (e.g. Federal Reserve, PBOC), inflation metrics (CPI, PPI), GDP figures.
- **General Everyday Knowledge**: Industry definitions, tax rules, financial product mechanics.

The AI assistant currently lacks real-time internet access, leading to hallucinations or knowledge cutoffs.

### Key Constraints & User Directives:
1. **Zero Cost / No API Key**: The solution must NOT depend on paid search APIs (Google Custom Search, Bing Web Search API, Tavily, SerpAPI). It must work out-of-the-box without requiring user registration or API keys.
2. **Comprehensive Coverage**: Financial market information, macro economics, and general knowledge.
3. **Deep Webpage Reading**: When search snippet summaries are insufficient, the AI must be able to automatically fetch and deeply read the full article body of specific target pages.
4. **Security & SSRF Defense**: As a self-hosted platform running on local home networks or private clouds, outgoing requests MUST prevent Server-Side Request Forgery (SSRF) against internal resources (`localhost`, `127.0.0.1`, `10.0.0.0/8`, `192.168.0.0/16`, `172.16.0.0/12`, `169.254.0.0/16`).

---

## 2. Architecture: Two-Tool Decoupled Model

Instead of a monolithic search-and-fetch tool that pollutes the model's token context on every query, we provide two specialized, decoupled ReAct tools registered in the unified AI Tool Registry (`services/ai/tools/`):

```
┌────────────────────────────────────────────────────────────────────────┐
│                          Unified AI Runtime                            │
│                 (Loop / Policy / Dispatcher / Trace)                   │
└───────────────────────────────────┬────────────────────────────────────┘
                                    │
           ┌────────────────────────┴────────────────────────┐
           ▼                                                 ▼
┌──────────────────────────────┐          ┌──────────────────────────────┐
│       web_search tool        │          │     fetch_webpage tool       │
│                              │          │                              │
│ • Query financial & general  │          │ • SSRF security validation   │
│ • Zero-key SERP aggregation  │          │ • Multi-hop redirect safety  │
│ • Direct & base64 URL unpack │          │ • Jina Reader Markdown clean │
│ • Extract Title, URL, Snippet│          │ • Direct HTML strip fallback │
│ • JSON structured output     │          │ • Token-safe truncation      │
└──────────────┬───────────────┘          └──────────────┬───────────────┘
               │                                         │
        HTTP GET (curl)                           HTTP GET (curl)
               │                                         │
               ▼                                         ▼
   ┌───────────────────────┐                 ┌───────────────────────┐
   │ Bing SERP / Wikipedia │                 │   r.jina.ai / Origin  │
   └───────────────────────┘                 └───────────────────────┘
```

1. **`web_search`**:
   - Takes a search query, returns a ranked list of search hits with clean titles, target URLs, and contextual snippet summaries.
   - Typically sufficient for answering factual questions (e.g., "What was the Fed's latest rate decision?", "Who won the Nobel prize in Economics 2026?").

2. **`fetch_webpage`**:
   - Invoked when the AI identifies a high-relevance URL from `web_search` and needs deep details (e.g. detailed financial statements, full regulatory documents, long-form investigative reports).
   - Extracts cleaned, structured Markdown text, stripped of ads, navbars, and boilerplate.

---

## 3. Tool Specifications

### 3.1 `web_search`

- **Name**: `web_search`
- **Description**: `"搜索互联网获取最新的财经市场资讯、宏观经济数据及通用知识。返回相关网页的标题、链接和摘要。"`
- **Permission**: `AI_PERM_READ`
- **Risk**: `AI_RISK_LOW`
- **Is Mutation**: `false`

#### Parameters Schema
```json
{
  "type": "object",
  "properties": {
    "query": {
      "type": "string",
      "description": "搜索关键词或短语，例如：'美联储 利率决议 2026' 或 '英伟达 财报 营收'"
    },
    "num_results": {
      "type": "integer",
      "description": "期望返回的结果数量，默认 5，范围 1-10"
    }
  },
  "required": ["query"]
}
```

#### Output Schema
```json
{
  "query": "英伟达 财报 2026",
  "count": 5,
  "results": [
    {
      "title": "NVIDIA 发布 2027 财年第二季度财务报告 | NVIDIA 英伟达博客",
      "url": "https://blogs.nvidia.cn/blog/nvidia-announces-financial-results-for-second-quarter-fiscal-2027/",
      "snippet": "2026年8月26日 — NVIDIA 今日宣布第二季度收入为 962 亿美元，较上一季度增长 18%..."
    }
  ]
}
```

#### Engine Implementation & Fallback Strategy
1. **Primary Engine**: Bing Search Scraper
   - Endpoint: `https://cn.bing.com/search?q={url_encoded_query}`
   - Request Headers: Standard desktop Chrome User-Agent, Accept-Language `zh-CN,zh;q=0.9,en;q=0.8`.
   - Timeout: 5000 ms.
   - Parsing: Extracts `<li class="b_algo">...` blocks.
     - **Title**: `<h2><a ...>(.*?)</a></h2>` (strips `<strong>`, `<b>` tags and unescapes HTML entities).
     - **URL**: `href="..."` attribute inside `<h2><a ...>`.
       - *Canonical URL Handling*: `cn.bing.com` provides canonical URLs directly in `href`. If a link matches Bing's click tracking pattern (`/ck/a?...&u=a1<base64>...`), the parser unescapes the base64url payload following `u=a1` to recover the canonical destination URL without extra network hops.
     - **Snippet**: `<div class="b_caption"><p class="b_lineclamp2"...>(.*?)</p></div>`.
2. **Fallback Engine**: Wikipedia OpenSearch API
   - If Bing returns zero results, network error, or anti-bot challenge:
   - Fallback to: `https://zh.wikipedia.org/w/api.php?action=opensearch&search={query}&limit=5&namespace=0&format=json`
   - Returns instantaneous encyclopedic knowledge for macro and general queries.

---

### 3.2 `fetch_webpage`

- **Name**: `fetch_webpage`
- **Description**: `"抓取并深入阅读指定网页的正文详情，提取清洗后的纯文本/Markdown内容。当搜索结果摘要不足以回答用户问题时使用。"`
- **Permission**: `AI_PERM_READ`
- **Risk**: `AI_RISK_LOW`
- **Is Mutation**: `false`

#### Parameters Schema
```json
{
  "type": "object",
  "properties": {
    "url": {
      "type": "string",
      "description": "要深入阅读的目标网页完整 URL（必须以 http:// 或 https:// 开头）"
    },
    "max_chars": {
      "type": "integer",
      "description": "提取的最大正文字符数，默认 4000，上限 10000，防止上下文溢出"
    }
  },
  "required": ["url"]
}
```

#### Output Schema
```json
{
  "url": "https://blogs.nvidia.cn/blog/nvidia-announces-financial-results-for-second-quarter-fiscal-2027/",
  "title": "NVIDIA 发布 2027 财年第二季度财务报告",
  "content": "NVIDIA 今日宣布截至 2026 年 7 月 26 日的第二季度收入为 962 亿美元...\n\n**GAAP 财务摘要**:\n收入: $96,221 百万美元...",
  "truncated": false
}
```

#### Security & SSRF Defense (STRICT)
Before executing any outgoing request:
1. **Dedicated AI URL Validator (`mf_ai_rule_validate_url`)**:
   - Location: `backend/src/domain/ai/rules.h/.c` (or `common/url_validator.h/.c`).
   - Sizing: Supports real-world web URLs up to 4096 bytes (unlike MCP entity config which is capped at 512 bytes).
   - Enforces `http://` or `https://` protocol only.
   - Rejects `localhost`, `127.0.0.1`, `0.0.0.0`, `::1`.
   - Rejects private IPv4 CIDRs: `10.0.0.0/8`, `172.16.0.0/12`, `192.168.0.0/16`, `169.254.0.0/16`.
   - Rejects unroutable or link-local hosts.
   - If invalid, immediately returns:
     `{"error": "forbidden_url", "message": "Access to local or private network addresses is prohibited for security."}`.
2. **Redirect SSRF Defense (Anti-Bypass)**:
   - When fetching fallback pages, `CURLOPT_FOLLOWLOCATION` is **disabled**.
   - Instead, the fetcher inspects HTTP status `301`, `302`, `303`, `307`, `308`.
   - On redirect, the `Location:` header is extracted and **re-validated** via `mf_ai_rule_validate_url` before following (max 3 hops). This completely prevents an external site from bouncing requests into `http://127.0.0.1:8080/api/...` or `192.168.x.x`.

#### Deep Fetching Engine
1. **Primary Extractor: Jina Reader Proxy (`r.jina.ai`)**:
   - URL: `https://r.jina.ai/{target_url}`
   - Headers: `Accept: text/plain`, User-Agent `Minefolio-AI/1.0`.
   - Timeout: 8000 ms.
   - Converts arbitrary web pages into clean, structured Markdown (including financial markdown tables).
   - Strips ads, scripts, navigation headers, footers automatically.
   - Free, zero authentication, publicly available.
2. **Fallback Extractor: Direct Fetch + HTML Stripper**:
   - If `r.jina.ai` is unreachable or times out:
   - Directly fetch `{target_url}` using `libcurl` (with 5000ms timeout and manual secure redirect verification).
   - HTML cleanup pipeline:
     1. Strip `<script>...</script>`, `<style>...</style>`, `<nav>...</nav>`, `<header>...</header>`, `<footer>...</footer>`.
     2. Extract `<title>...</title>`.
     3. Strip all remaining HTML tags `<[^>]+>`.
     4. Unescape common HTML entities (`&nbsp;`, `&amp;`, `&lt;`, `&gt;`, `&quot;`, `&#39;`).
     5. Collapse multiple blank lines into single line breaks.
3. **Token Safety Truncation**:
   - Truncates body to `max_chars` (default 4000, capped at 10000).
   - Appends `\n\n[Content truncated due to character limit]` if truncated, setting `"truncated": true`.

---

## 4. Error Handling & Resilience

| Scenario | Behavior |
|----------|----------|
| Network timeout (curl error 28) | Returns `{"error": "timeout", "message": "Search request timed out after 5s."}` |
| SSRF attempt (`192.168.1.1` or `127.0.0.1`) | Blocked immediately with `{"error": "forbidden_url"}` before network request |
| Malicious redirect to internal IP | Blocked at redirect hop evaluation with `{"error": "forbidden_redirect_url"}` |
| Missing or malformed query/url | Returns `{"error": "invalid_parameters"}` |
| Target webpage returns 404/403 | Returns `{"error": "fetch_failed", "status_code": 403, "url": "..."}` |
| Parsing returns 0 results | Falls back to Wikipedia; if still empty, returns `{"query": "...", "count": 0, "results": []}` |

---

## 5. File Structure & Changes

1. **`backend/src/domain/ai/rules.h` & `backend/src/domain/ai/rules.c`**:
   - Add `bool mf_ai_rule_validate_url(const char* url, size_t max_len)` (SSRF prevention, up to 4096 bytes).
2. **`backend/src/services/ai/tools/web_search_tool.h`**:
   - Tool declaration: `void ai_tool_web_search_register_all(void);`
3. **`backend/src/services/ai/tools/web_search_tool.c`**:
   - Implementation of `web_search` and `fetch_webpage` tools.
   - Helper functions: `curl_http_get_with_redirect_check()`, `bing_html_parse()`, `bing_unpack_url()`, `html_strip_tags()`, `html_unescape()`.
4. **`backend/src/services/ai/tools/registry.c`**:
   - Include `services/ai/tools/web_search_tool.h`.
   - In `registry_init_impl()`: call `ai_tool_web_search_register_all()`.
5. **`backend/tests/unit/test_ai_tools.c`**:
   - Add unit tests covering:
     - SSRF protection on direct and redirected URLs (up to 4096 bytes).
     - Parameter schema validation.
     - Bing SERP HTML parsing and URL unpacking fixture.
     - Tool registration in registry.

---

## 6. Verification Plan

1. **Unit Tests**:
   - `ctest -R test_ai_tools --output-on-failure`
2. **Integration Verification via cURL / Backend**:
   - Test AI chat tool invocation with query requiring real-time web search.
   - Verify tool output JSON schema and tokens.
3. **Full Regression**:
   - Run all 35 CTest suites: `ctest --output-on-failure` (100% pass required).
   - Frontend build check: `npm --prefix frontend run build`.
