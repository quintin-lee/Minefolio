import http from '@/utils/http'
import type { Ledger, LedgerMember, LedgerInviteResult } from '@/types'

export const ledgerApi = {
  list: () =>
    http.get<Ledger[], Ledger[]>('/ledgers'),

  create: (data: Partial<Ledger>) =>
    http.post<{ id: number }, { id: number }>('/ledgers', data),

  get: (id: number) =>
    http.get<Ledger[], Ledger[]>(`/ledgers/${id}`),

  update: (id: number, data: Partial<Ledger>) =>
    http.put<void, void>(`/ledgers/${id}`, data),

  delete: (id: number) =>
    http.delete<void, void>(`/ledgers/${id}`),

  listMembers: (id: number) =>
    http.get<LedgerMember[], LedgerMember[]>(`/ledgers/${id}/members`),

  addMember: (id: number, username: string, role: 'editor' | 'viewer' = 'editor') =>
    http.post<void, void>(`/ledgers/${id}/members`, { username, role }),

  updateMember: (id: number, userId: number, role: 'editor' | 'viewer') =>
    http.put<void, void>(`/ledgers/${id}/members/${userId}`, { role }),

  removeMember: (id: number, userId: number) =>
    http.delete<void, void>(`/ledgers/${id}/members/${userId}`),

  createInviteCode: (id: number) =>
    http.post<LedgerInviteResult, LedgerInviteResult>(`/ledgers/${id}/invite-code`),

  joinByInvite: (inviteCode: string) =>
    http.post<{ id: number; name: string }, { id: number; name: string }>('/ledgers/join', {
      invite_code: inviteCode
    })
}
