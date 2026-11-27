import http from '@/utils/http'
import type { PageResult } from '@/types'

export type AsyncTaskStatus = 'pending' | 'running' | 'completed' | 'failed'

export interface AsyncTask<T = unknown> {
  id: number
  user_id: number
  ledger_id: number
  task_type: string
  status: AsyncTaskStatus
  progress: number
  message: string
  error_message: string
  result: T | null
  created_at: string
  started_at: string
  completed_at: string
  updated_at: string
}

export interface AsyncTaskSubmission {
  task_id: number
  task_type?: string
  status: AsyncTaskStatus
  deduplicated?: boolean
}

export function isAsyncTaskSubmission(val: unknown): val is AsyncTaskSubmission {
  return Boolean(
    val &&
    typeof val === 'object' &&
    typeof (val as AsyncTaskSubmission).task_id === 'number' &&
    (val as AsyncTaskSubmission).task_id > 0
  )
}

export interface PollTaskOptions {
  intervalMs?: number
  timeoutMs?: number
  signal?: AbortSignal
  onProgress?: (task: AsyncTask<unknown>) => void
}

export class TaskCancelledError extends Error {
  constructor(message = '任务轮询已取消') {
    super(message)
    this.name = 'TaskCancelledError'
  }
}

export const tasksApi = {
  get: <T = unknown>(id: number, signal?: AbortSignal) =>
    signal ? http.get<AsyncTask<T>>(`/tasks/${id}`, { signal }) : http.get<AsyncTask<T>>(`/tasks/${id}`),
  list: (params?: { status?: string; task_type?: string; page?: number; page_size?: number; limit?: number }) =>
    http.get<PageResult<AsyncTask>>('/tasks', { params }),
}

function waitWithSignal(ms: number, signal?: AbortSignal): Promise<void> {
  if (!signal) {
    return new Promise((resolve) => setTimeout(resolve, ms))
  }
  if (signal.aborted) {
    return Promise.reject(new TaskCancelledError())
  }
  return new Promise((resolve, reject) => {
    const onAbort = () => {
      clearTimeout(timer)
      reject(new TaskCancelledError())
    }
    const timer = setTimeout(() => {
      signal.removeEventListener('abort', onAbort)
      resolve()
    }, ms)
    signal.addEventListener('abort', onAbort, { once: true })
  })
}

/**
 * Poll GET /api/tasks/:id until completion or failure.
 */
export async function pollTask<T = unknown>(
  taskId: number,
  options: PollTaskOptions = {},
): Promise<T> {
  const intervalMs = options.intervalMs ?? 300
  const timeoutMs = options.timeoutMs ?? 60000
  const deadline = Date.now() + timeoutMs

  while (true) {
    if (options.signal?.aborted) {
      throw new TaskCancelledError()
    }
    let task: AsyncTask<T>
    try {
      task = await tasksApi.get<T>(taskId, options.signal)
    } catch (err) {
      if (options.signal?.aborted) {
        throw new TaskCancelledError()
      }
      throw err
    }
    options.onProgress?.(task)

    if (task.status === 'completed') {
      return task.result as T
    }
    if (task.status === 'failed') {
      throw new Error(task.error_message || task.message || '后台任务执行失败')
    }
    if (Date.now() >= deadline) {
      throw new Error('任务处理超时，请稍后在任务列表中查看结果')
    }

    await waitWithSignal(intervalMs, options.signal)
  }
}

export async function resolveMaybeAsync<T>(
  response: T | AsyncTaskSubmission,
  options?: PollTaskOptions,
): Promise<T> {
  if (isAsyncTaskSubmission(response)) {
    return pollTask<T>(response.task_id, options)
  }
  return response
}
