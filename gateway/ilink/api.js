'use strict';

class GatewayHttpError extends Error {
  constructor(message, { code = 0, http = 500 } = {}) {
    super(message);
    this.name = 'GatewayHttpError';
    this.code = code;
    this.http = http;
  }
}

function createGatewayApi({
  apiBase = process.env.MINEFOLIO_API || 'http://127.0.0.1:8080',
  gatewayToken = process.env.MINEFOLIO_GATEWAY_TOKEN || '',
  fetchImpl = fetch,
  timeoutMs = 15000,
} = {}) {
  const base = String(apiBase).replace(/\/+$/, '');

  async function request(method, path, body) {
    if (!gatewayToken) {
      throw new GatewayHttpError('MINEFOLIO_GATEWAY_TOKEN is not configured', {
        code: 1001,
        http: 401,
      });
    }
    const headers = {
      'X-Gateway-Token': gatewayToken,
    };
    const init = {
      method,
      headers,
      signal: AbortSignal.timeout(timeoutMs),
    };
    if (body !== undefined) {
      headers['Content-Type'] = 'application/json';
      init.body = JSON.stringify(body);
    }
    const res = await fetchImpl(`${base}${path}`, init);
    const json = await res.json().catch(() => null);
    if (!json || typeof json !== 'object') {
      throw new GatewayHttpError(`Invalid JSON response (${res.status})`, {
        code: res.status || 500,
        http: res.status || 500,
      });
    }
    if (typeof json.code === 'number' && json.code !== 0) {
      throw new GatewayHttpError(json.message || `Gateway API error ${json.code}`, {
        code: json.code,
        http: res.status || 200,
      });
    }
    return json.data;
  }

  async function listInstances() {
    const data = await request('GET', '/api/gateway/im/instances');
    if (Array.isArray(data)) return data;
    if (data && Array.isArray(data.instances)) return data.instances;
    return [];
  }

  async function getInstance(id) {
    return request('GET', `/api/gateway/im/instances/${id}`);
  }

  async function updateStatus(id, payload) {
    return request('PATCH', `/api/gateway/im/instances/${id}/status`, payload);
  }

  async function heartbeat(id, payload = {}) {
    return request('POST', `/api/gateway/im/instances/${id}/heartbeat`, payload);
  }

  async function chat(id, imUserId, message) {
    return request('POST', `/api/gateway/im/instances/${id}/chat`, {
      im_user_id: imUserId,
      message,
    });
  }

  async function confirm(id, imUserId, code) {
    return request('POST', `/api/gateway/im/instances/${id}/confirm`, {
      im_user_id: imUserId,
      code,
    });
  }

  async function getTask(id, taskId) {
    return request('GET', `/api/gateway/im/instances/${id}/tasks/${taskId}`);
  }

  async function pollTask(
    id,
    taskId,
    { intervalMs = 150, timeoutMs: pollTimeoutMs = 60000, signal } = {}
  ) {
    const deadline = Date.now() + pollTimeoutMs;
    while (Date.now() < deadline) {
      if (signal && signal.aborted) {
        throw new Error('aborted');
      }
      const task = await getTask(id, taskId);
      if (task && task.status === 'completed') {
        return task.result || {};
      }
      if (task && (task.status === 'failed' || task.status === 'cancelled')) {
        throw new Error(task.error_message || `task ${task.status}`);
      }
      await new Promise((r) => setTimeout(r, intervalMs));
    }
    throw new Error('AI 查询超时，请稍后再试');
  }

  async function claimPushes(id, payload = {}) {
    return request('POST', `/api/gateway/im/instances/${id}/pushes/claim`, payload);
  }

  async function ackPush(id, pushId, payload = {}) {
    return request('POST', `/api/gateway/im/instances/${id}/pushes/${pushId}/ack`, payload);
  }

  return {
    listInstances,
    getInstance,
    updateStatus,
    heartbeat,
    chat,
    confirm,
    getTask,
    pollTask,
    claimPushes,
    ackPush,
  };
}

module.exports = {
  GatewayHttpError,
  createGatewayApi,
};
