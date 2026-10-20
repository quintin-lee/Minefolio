import type { ProposedAction } from '@/components/ActionCard.vue'
import type { WorkflowRunState, WorkflowConfigState } from '@/types'

export type PanelItemType =
  | 'mermaid'
  | 'markdown'
  | 'action'
  | 'code'
  | 'workflow-progress'
  | 'workflow-config'

export interface PanelItem {
  id: string
  label: string
  icon: string
  type: PanelItemType
  content: string
  lang?: string
  actionData?: ProposedAction
  workflowData?: WorkflowRunState
  workflowConfig?: WorkflowConfigState
  messageId?: number
  segmentId?: string
  createdAt: number
}

export interface ExtractResult {
  panelItems: PanelItem[]
  extractedSegmentIds: Set<string>
}
