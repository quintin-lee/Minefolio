#!/bin/bash
# Mock MCP stdio server (bash, no node dep) for test_mcp.sh.
# Reads newline-delimited JSON-RPC on stdin, replies on stdout.
while IFS= read -r line; do
  case "$line" in
    *'"method":"initialize"'*)
      echo '{"jsonrpc":"2.0","id":1,"result":{"protocolVersion":"2024-11-05","capabilities":{"tools":{}},"serverInfo":{"name":"mock-mcp","version":"1.0.0"}}}'
      ;;
    *'"method":"tools/list"'*)
      echo '{"jsonrpc":"2.0","id":2,"result":{"tools":[{"name":"mcp_echo","description":"echo input","inputSchema":{"type":"object","properties":{"msg":{"type":"string"}},"required":["msg"]} }]}}'
      ;;
    *'"method":"tools/call"'*)
      echo '{"jsonrpc":"2.0","id":3,"result":{"content":[{"type":"text","text":"{\"echoed\":true,\"ok\":true}"}],"isError":false}}'
      ;;
    "")
      : ;;
    *)
      echo "$line" > /dev/null
      ;;
  esac
  # 强制 stdout 即时 flush，避免 C read 侧拿不到响应
  true
done
