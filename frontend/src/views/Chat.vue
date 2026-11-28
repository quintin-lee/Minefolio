<template>
  <div class="chat-page">
    <div class="chat-layout">
      <!-- Sidebar overlay backdrop (mobile & tablet drawer) -->
      <div class="sidebar-overlay" v-if="showSidebar && isSidebarDrawer" @click="showSidebar = false"></div>

      <!-- Sidebar: session list -->
      <aside class="chat-sidebar" :class="{ 'is-collapsed': !showSidebar }">
        <div class="sidebar-header">
          <div class="sidebar-title">
            <Icon icon="ph:chat-circle-text-bold" />
            <span>历史会话</span>
            <span class="session-count" v-if="chat.sessions.length > 0">({{ chat.sessions.length }})</span>
          </div>
          <div class="sidebar-header-actions">
            <el-tooltip content="新建会话" placement="bottom" :show-after="300">
              <button class="icon-header-btn" @click="handleNewChat" aria-label="新建会话">
                <Icon icon="ph:plus-bold" />
              </button>
            </el-tooltip>
            <el-tooltip content="收起会话列表" placement="bottom" :show-after="300">
              <button class="icon-close-btn" @click="showSidebar = false" title="收起会话列表" aria-label="收起会话列表">
                <Icon icon="ph:sidebar-simple" />
              </button>
            </el-tooltip>
          </div>
        </div>
        <div class="session-search-wrap" v-if="chat.sessions.length > 3">
          <Icon icon="ph:magnifying-glass" class="session-search-icon" />
          <input v-model="sessionSearch" class="session-search-input" placeholder="搜索会话..." aria-label="搜索会话" />
          <button v-if="sessionSearch" class="session-search-clear" @click="sessionSearch = ''" title="清空">
            <Icon icon="ph:x" />
          </button>
        </div>
        <div class="session-list" v-loading="loadingSessions">
          <template v-if="groupedSessions.length > 0">
            <template v-for="group in groupedSessions" :key="group.label">
              <div class="session-group-label" :class="{ 'pinned-group-label': group.isPinnedGroup }">{{ group.label }}</div>
              <div
                v-for="s in group.items"
                :key="s.id"
                class="session-item"
                :class="{ active: chat.currentSessionId === s.id, pinned: chat.isSessionPinned(s.id) }"
                @click="selectSession(s.id)"
                role="listitem"
              >
                <div v-if="editingSessionId === s.id" class="session-rename-box" @click.stop>
                  <input
                    v-focus
                    v-model="renameTitle"
                    class="session-rename-input"
                    @keydown.enter="saveRename(s.id)"
                    @keydown.esc="cancelRename"
                    @blur="saveRename(s.id)"
                  />
                </div>
                <template v-else>
                  <div class="session-item-content">
                    <div class="session-title" :title="s.title" @dblclick.stop="startRename(s)">
                      <span v-if="getSessionTag(s.title)" class="session-scenario-tag" :title="s.title">
                        {{ getSessionTag(s.title) }}
                      </span>
                      <span class="session-title-text">{{ s.title }}</span>
                      <Icon
                        v-if="chat.isSessionPinned(s.id)"
                        icon="ph:push-pin-fill"
                        class="session-pin-icon"
                        title="已置顶"
                      />
                    </div>
                    <div class="session-meta">{{ formatTime(s.updated_at) }}</div>
                  </div>
                  <div class="session-item-actions" @click.stop>
                    <el-tooltip :content="chat.isSessionPinned(s.id) ? '取消置顶' : '置顶会话'" placement="top" :show-after="300">
                      <button
                        class="item-action-btn pin-btn"
                        :class="{ 'is-pinned': chat.isSessionPinned(s.id) }"
                        :title="chat.isSessionPinned(s.id) ? '取消置顶' : '置顶会话'"
                        @click.stop="chat.togglePinSession(s.id)"
                      >
                        <Icon :icon="chat.isSessionPinned(s.id) ? 'ph:push-pin-slash' : 'ph:push-pin'" />
                      </button>
                    </el-tooltip>
                    <el-dropdown trigger="click" placement="bottom-end">
                      <button class="item-action-btn session-more-btn" title="更多操作">
                        <Icon icon="ph:dots-three-vertical-bold" />
                      </button>
                      <template #dropdown>
                        <el-dropdown-menu class="session-action-dropdown">
                          <el-dropdown-item @click="chat.togglePinSession(s.id)">
                            <Icon :icon="chat.isSessionPinned(s.id) ? 'ph:push-pin-slash' : 'ph:push-pin'" class="dropdown-item-icon" />
                            <span>{{ chat.isSessionPinned(s.id) ? '取消置顶' : '置顶会话' }}</span>
                          </el-dropdown-item>
                          <el-dropdown-item @click="handleExportSingleSession(s)">
                            <Icon icon="ph:download-simple" class="dropdown-item-icon" />
                            <span>导出为 Markdown</span>
                          </el-dropdown-item>
                          <el-dropdown-item @click="startRename(s)">
                            <Icon icon="ph:pencil-simple" class="dropdown-item-icon" />
                            <span>重命名</span>
                          </el-dropdown-item>
                          <el-dropdown-item divided @click="handleDeleteSession(s.id)" class="danger-item">
                            <Icon icon="ph:trash" class="dropdown-item-icon" />
                            <span>删除会话</span>
                          </el-dropdown-item>
                        </el-dropdown-menu>
                      </template>
                    </el-dropdown>
                  </div>
                </template>
              </div>
            </template>
          </template>
          <div v-if="filteredSessions.length === 0 && !loadingSessions" class="empty-sessions">
            <Icon icon="ph:chats" class="empty-sessions-icon" />
            <span>{{ sessionSearch ? '无匹配会话' : '暂无历史会话' }}</span>
          </div>
        </div>
      </aside>

      <!-- Main chat area -->
      <main class="chat-main">
        <!-- Integrated Chat Toolbar -->
        <div class="chat-toolbar" :class="{ 'with-toggle': !showSidebar }">
          <div class="toolbar-left">
            <!-- Toggle sidebar button (when sidebar is collapsed) -->
            <el-tooltip v-if="!showSidebar" content="展开会话列表" placement="bottom" :show-after="300">
              <button class="toggle-sidebar-btn" @click="showSidebar = true" aria-label="展开会话列表">
                <Icon icon="ph:sidebar-simple" />
              </button>
            </el-tooltip>
          </div>

          <div class="toolbar-center">
            <!-- Session Title & Inline Rename (Left-aligned for natural document hierarchy) -->
            <div class="session-title-wrap" :title="currentSessionTitle" @dblclick="startTopRename">
              <div v-if="editingTopTitle" class="top-rename-box" @click.stop>
                <input
                  v-focus
                  v-model="topRenameText"
                  class="top-rename-input"
                  placeholder="输入会话标题..."
                  @keydown.enter="saveTopRename"
                  @keydown.esc="cancelTopRename"
                  @blur="saveTopRename"
                />
              </div>
              <template v-else>
                <span class="top-session-title">{{ currentSessionTitle }}</span>
                <button
                  v-if="chat.currentSessionId"
                  class="top-rename-btn"
                  title="重命名会话"
                  aria-label="重命名会话"
                  @click.stop="startTopRename"
                >
                  <Icon icon="ph:pencil-simple" />
                </button>
              </template>
            </div>

            <div class="chat-status" v-if="chat.isStreaming">
              <span class="status-dot"></span>
              <span class="status-text">生成中</span>
              <button class="stop-gen-btn" @click="handleStop" title="停止生成 (Esc)">
                <Icon icon="ph:stop-bold" />
                <span>停止</span>
              </button>
            </div>
          </div>

          <div class="toolbar-right">
            <!-- Compact Model Selector Pill -->
            <el-dropdown trigger="click" placement="bottom-end" @command="(val: string) => selectedModelKey = val">
              <button class="model-pill-btn" :title="`当前模型: ${currentModelProvider ? currentModelProvider + ' · ' : ''}${currentModelDisplay}`">
                <Icon icon="ph:cpu-bold" class="model-pill-icon" />
                <span class="model-pill-text">{{ currentModelDisplay }}</span>
                <Icon icon="ph:caret-down-bold" class="model-pill-caret" />
              </button>
              <template #dropdown>
                <el-dropdown-menu class="model-select-dropdown">
                  <el-dropdown-item
                    v-for="opt in modelOptions"
                    :key="opt.value"
                    :command="opt.value"
                    :class="{ 'is-active-model': selectedModelKey === opt.value }"
                  >
                    <div class="model-option-card">
                      <div class="model-option-meta">
                        <span class="opt-provider-tag">{{ opt.provider_name }}</span>
                        <span class="opt-name-main">{{ opt.model }}</span>
                      </div>
                      <Icon v-if="selectedModelKey === opt.value" icon="ph:check-bold" class="model-check-icon" />
                    </div>
                  </el-dropdown-item>
                </el-dropdown-menu>
              </template>
            </el-dropdown>
            <!-- Session Metrics Pill -->
            <div
              v-if="sessionMetrics.totalTokens > 0"
              class="session-stat-pill"
              :title="`本场累计消耗 ${sessionMetrics.totalTokens.toLocaleString()} tokens，预估费用 $${sessionMetrics.totalCostUsd.toFixed(4)}`"
            >
              <Icon icon="ph:lightning-fill" class="stat-icon" />
              <span class="stat-tokens">{{ sessionMetrics.totalTokens.toLocaleString() }}</span>
              <span class="stat-divider">·</span>
              <span class="stat-cost">${{ sessionMetrics.totalCostUsd.toFixed(4) }}</span>
            </div>

            <!-- Quick Analysis Panel Toggle -->
            <button
              v-if="panelItems.length > 0"
              class="toolbar-panel-btn"
              :class="{ active: panelVisible }"
              :title="panelVisible ? '收起分析面板' : '展开分析面板'"
              @click="panelVisible = !panelVisible"
            >
              <Icon icon="ph:sparkle-bold" class="panel-btn-icon" />
              <span class="panel-badge">{{ panelItems.length }}</span>
            </button>

            <!-- New Chat Action -->
            <el-button
              type="primary"
              size="small"
              plain
              class="toolbar-action-btn new-chat-btn"
              @click="handleNewChat"
              title="新建会话 (Ctrl+N)"
            >
              <Icon icon="ph:plus-bold" class="btn-icon" />
              <span class="btn-text">新对话</span>
            </el-button>

            <!-- More Actions Dropdown -->
            <el-dropdown trigger="click" placement="bottom-end">
              <button class="more-menu-btn" title="更多操作" aria-label="更多操作">
                <Icon icon="ph:dots-three-vertical-bold" />
              </button>
              <template #dropdown>
                <el-dropdown-menu class="chat-more-dropdown">
                  <el-dropdown-item
                    @click="exportToMarkdown"
                    :disabled="chat.messages.length === 0"
                  >
                    <Icon icon="ph:download-simple" class="dropdown-item-icon" />
                    <span>导出 Markdown</span>
                  </el-dropdown-item>
                  <el-dropdown-item
                    @click="copyAllChat"
                    :disabled="chat.messages.length === 0"
                  >
                    <Icon icon="ph:copy" class="dropdown-item-icon" />
                    <span>复制全文</span>
                  </el-dropdown-item>
                  <el-dropdown-item @click="goToTraces">
                    <Icon icon="ph:chart-line-up" class="dropdown-item-icon" />
                    <span>AI 审计追踪</span>
                  </el-dropdown-item>
                  <el-dropdown-item
                    divided
                    @click="handleClearChat"
                    :disabled="chat.messages.length === 0"
                    class="danger-item"
                  >
                    <Icon icon="ph:trash" class="dropdown-item-icon" />
                    <span>清空本场记录</span>
                  </el-dropdown-item>
                </el-dropdown-menu>
              </template>
            </el-dropdown>
          </div>
        </div>

        <!-- Messages stream container -->
        <div class="messages-container">
          <div
            class="messages"
            ref="messagesRef"
            v-loading="loadingMessages"
            @scroll.passive="handleScroll"
            @wheel.passive="handleWheel"
            @touchstart.passive="handleTouchStart"
            @touchmove.passive="handleTouchMove"
            role="log"
            aria-live="polite"
            aria-label="对话消息"
          >
            <!-- Load more trigger (invisible sentinel at top) -->
            <div ref="loadMoreTrigger" class="load-more-sentinel"></div>
            <div v-if="chat.loadingMoreMessages" class="load-more-spinner">
              <span class="spinner-dot"></span><span class="spinner-dot"></span><span class="spinner-dot"></span>
            </div>
            <!-- Empty state: Quick prompts -->
            <PromptStarters v-if="chat.messages.length === 0" @select="handleQuickPrompt" />

            <!-- Message list -->
            <template v-else>
              <div
                v-for="(msg, idx) in chat.messages"
                :key="msg.id"
                :ref="el => { if (idx === chat.messages.length - 1) onLastMessageRef(el as HTMLElement | null) }"
                v-memo="[
                  msg.content,
                  msg.thinking_content || '',
                  msg.role,
                  idx === chat.messages.length - 1 ? 1 : 0,
                  chat.isStreaming && idx === chat.messages.length - 1 ? 1 : 0,
                  editingUserIdx === idx ? editDraft : null,
                  msg.workflowData?.status || '',
                  msg.workflowData?.progress ?? -1,
                  msg.workflowData?.steps?.map(s => s.status).join(',') || '',
                  msg.tool_calls?.length || 0,
                  msg.tool_calls?.[msg.tool_calls.length - 1]?.result?.length || 0,
                  msg.workflowConfig ? 1 : 0,
                  msg.error || '',
                  msg.usage?.total_tokens ?? -1,
                  extractedSegmentIdMap[msg.id]?.length ?? 0,
                  idx === chat.messages.length - 1 ? followUpSuggestions.length : 0,
                ]"
                class="message-row"
                :class="[msg.role, { 'streaming-active-row': chat.isStreaming && idx === chat.messages.length - 1 }]"
              >
                <div class="message-avatar">
                  <Icon :icon="msg.role === 'user' ? 'ph:user-bold' : 'ph:robot-bold'" />
                </div>
                <div class="message-bubble-wrap">
                  <template v-if="msg.role === 'user'">
                    <div v-if="editingUserIdx === idx" class="user-edit-box">
                      <textarea v-model="editDraft" class="user-edit-input" rows="2" @keydown.enter.exact.prevent="confirmEdit(idx)" @keydown.esc="cancelEdit" placeholder="编辑后按 Enter 发送，Esc 取消"></textarea>
                      <div class="user-edit-actions">
                        <el-button size="small" @click="cancelEdit">取消</el-button>
                        <el-button size="small" type="primary" :disabled="!editDraft.trim()" @click="confirmEdit(idx)">发送</el-button>
                      </div>
                    </div>
                    <template v-else>
                      <div class="message-content">{{ msg.content }}</div>
                      <div v-if="!chat.isStreaming" class="message-actions user-actions">
                      <button class="msg-action-btn" title="复制" @click="copyText(msg.content)" aria-label="复制消息">
                        <Icon icon="ph:copy" />
                        <span>复制</span>
                      </button>
                      <button class="msg-action-btn" title="编辑并重发" @click="startEdit(idx, msg.content)" aria-label="编辑并重发">
                        <Icon icon="ph:pencil-simple" />
                        <span>编辑</span>
                      </button>
                      </div>
                    </template>
                  </template>
                  <template v-else>
                    <div v-if="!msg.content && chat.isStreaming && !msg.workflowData && !msg.workflowConfig && !msg.thinking_content && !msg.tool_calls?.length" class="message-content thinking">
                      <span class="pulse-dot"></span>
                      <span class="pulse-dot"></span>
                      <span class="pulse-dot"></span>
                      <span class="thinking-text">思考中...</span>
                    </div>
                    <div v-else class="message-content" :class="{ 'streaming-active': chat.isStreaming && idx === chat.messages.length - 1 && msg.role === 'assistant' }">
                      <ChatMessageContent
                        :message-id="msg.id"
                        :content="msg.content"
                        :thinking-content="msg.thinking_content"
                        :tool-calls="msg.tool_calls"
                        :workflow-data="msg.workflowData"
                        :workflow-config="msg.workflowConfig"
                        :is-streaming="chat.isStreaming && idx === chat.messages.length - 1 && msg.role === 'assistant'"
                        :enable-buffer="chat.enableTypewriterBuffer"
                        :extracted-item-ids="extractedSegmentIdMap[msg.id] || []"
                        @open-panel-item="handleOpenPanelItem"
                        @entity-hover="handleEntityHover"
                        @entity-leave="handleEntityLeave"
                        @entity-click="handleEntityClick"
                        @action-executed="handleActionExecuted(msg, $event)"
                      />
                    </div>
                    <div v-if="msg.usage" class="message-usage">
                      <span v-if="msg.usage.total_tokens">⚡ {{ msg.usage.total_tokens.toLocaleString() }} tokens · ${{ msg.usage.cost_usd.toFixed(4) }}</span>
                      <span v-if="msg.usage.elapsed_ms"> · {{ (msg.usage.elapsed_ms / 1000).toFixed(1) }}s</span>
                      <span v-if="msg.usage.finish_reason && msg.usage.finish_reason !== 'stop'" class="budget-tag" :title="'结束原因: ' + msg.usage.finish_reason">{{ msg.usage.finish_reason }}</span>
                    </div>
                    <div v-if="msg.error" class="message-error">
                      <Icon icon="ph:warning-circle-fill" />
                      <span>{{ msg.error }}</span>
                    </div>
                    <div v-if="(msg.content || msg.thinking_content || msg.error) && !chat.isStreaming" class="message-actions">
                      <button v-if="msg.content || msg.thinking_content" class="msg-action-btn" title="复制回答" @click="copyText(msg.content || msg.thinking_content || '')" aria-label="复制回答">
                        <Icon icon="ph:copy" />
                        <span>复制</span>
                      </button>
                      <button v-if="idx === chat.messages.length - 1" class="msg-action-btn" title="重新生成" @click="handleRegenerate" aria-label="重新生成">
                        <Icon icon="ph:arrows-counter-clockwise" />
                        <span>重新生成</span>
                      </button>
                      <button v-if="(msg.content && msg.content.startsWith('⚠️')) || msg.error" class="msg-action-btn" title="重试" @click="handleRetryMessage(msg.id)" aria-label="重试">
                        <Icon icon="ph:arrow-clockwise" />
                        <span>重试</span>
                      </button>
                    </div>

                    <!-- Smart Follow-up Suggestion Chips -->
                    <div
                      v-if="idx === chat.messages.length - 1 && !chat.isStreaming && followUpSuggestions.length > 0"
                      class="followup-chips-wrap"
                    >
                      <div class="followup-title">
                        <Icon icon="ph:sparkle-fill" class="followup-icon" />
                        <span>建议下一步探索</span>
                      </div>
                      <div class="followup-chips-list">
                        <button
                          v-for="(chip, chipIdx) in followUpSuggestions"
                          :key="chipIdx"
                          type="button"
                          class="followup-chip-btn"
                          @click="handleSelectFollowup(chip)"
                        >
                          <Icon :icon="chip.icon || 'ph:arrow-elbow-down-right-bold'" class="chip-icon" />
                          <span>{{ chip.text }}</span>
                        </button>
                      </div>
                    </div>
                  </template>
                </div>
              </div>
            </template>
          </div>

          <button
            v-show="chat.messages.length > 0 && showScrollBottomBtn"
            class="scroll-bottom-btn"
            :class="{ 'has-new-stream': chat.isStreaming }"
            ref="scrollBtnRef"
            title="回到底部"
            @click="forceScrollToBottom"
          >
            <span v-if="chat.isStreaming" class="btn-streaming-dot"></span>
            <Icon icon="ph:arrow-down-bold" />
            <span>{{ chat.isStreaming ? '有新内容 · 回到底部' : '回到底部' }}</span>
          </button>
        </div>

        <!-- Input Area -->
        <div class="chat-input-area" ref="inputAreaRef">
          <!-- Financial AI Workflows Trigger Bar in Progressive Wrapper -->
          <div class="workflow-bar-wrapper" :class="{ 'is-collapsed': isWorkflowCollapsed }">
            <WorkflowBar />
          </div>

          <div
            class="chat-input-box"
            :class="{ 'is-dragover': isDragOver }"
            style="position: relative;"
            @paste="handlePaste"
            @dragover.prevent="isDragOver = true"
            @dragleave.prevent="isDragOver = false"
            @drop.prevent="handleDrop"
          >
            <!-- Attachment Preview Capsule -->
            <div v-if="attachedImage" class="attachment-pill">
              <div class="attachment-thumb-wrap">
                <img :src="attachedImage.dataUrl" class="attachment-thumb" alt="票据截图" />
              </div>
              <div class="attachment-meta">
                <span class="attachment-name" :title="attachedImage.name">{{ attachedImage.name }}</span>
                <span class="attachment-size">{{ attachedImage.sizeText }} · 待 AI 多模态识别</span>
              </div>
              <button
                type="button"
                class="attachment-remove-btn"
                title="移除图片"
                aria-label="移除图片"
                @click="removeAttachment"
              >
                <Icon icon="ph:x-bold" />
              </button>
            </div>

            <WorkflowSlashMenu
              ref="slashMenuRef"
              :visible="showSlashMenu"
              :query="slashQuery"
              @select="handleSelectWorkflow"
              @close="showSlashMenu = false"
            />
            <AssetMentionMenu
              ref="mentionMenuRef"
              :visible="showMentionMenu"
              :query="mentionQuery"
              @select="handleSelectMention"
              @close="showMentionMenu = false"
            />
            <textarea
              ref="textareaRef"
              v-model="inputText"
              :disabled="chat.isStreaming || isScanningReceipt"
              maxlength="2000"
              placeholder="输入财务咨询、收支分析，或按 Ctrl+V 粘贴票据截图... (Enter 发送，Shift+Enter 换行)"
              rows="1"
              @input="handleInput"
              @keydown="handleTextareaKeyDown"
            ></textarea>
            <div class="input-bottom-bar">
              <div class="input-bottom-left">
                <label class="input-attach-btn" :class="{ 'has-file': !!attachedImage }" title="上传或粘贴发票/账单截图 (Ctrl+V)">
                  <Icon icon="ph:image-square-bold" class="btn-icon" />
                  <span>{{ attachedImage ? '更换票据' : '上传票据' }}</span>
                  <input
                    type="file"
                    accept="image/*"
                    @change="handleFileSelect"
                    style="display: none;"
                  />
                </label>
                <button
                  type="button"
                  class="input-thinking-btn"
                  :class="{ active: chat.enableThinking }"
                  @click="chat.toggleThinking()"
                  :title="chat.enableThinking ? '深度思考模式已开启 (点击关闭)' : '深度思考模式已关闭 (点击开启)'"
                  aria-label="切换深度思考模式"
                >
                  <Icon :icon="chat.enableThinking ? 'ph:brain-fill' : 'ph:brain'" class="btn-icon" />
                  <span>深度思考</span>
                  <span class="thinking-dot" v-if="chat.enableThinking"></span>
                </button>
              </div>
              <div class="input-bottom-right">
                <span class="char-count" :class="{ near: inputText.length > 1800, over: inputText.length > 2000 }">{{ inputText.length }} / 2000</span>
                <span class="input-hint" v-if="!chat.isStreaming && !isScanningReceipt">↵ 发送 · ⇧↵ 换行 · Esc 停止</span>
                <span class="input-hint scanning-hint" v-else-if="isScanningReceipt">正在多模态智能识别票据中...</span>
                <el-button v-if="chat.isStreaming" type="danger" class="send-btn stop-btn" @click="handleStop">
                  <Icon icon="ph:stop-circle-bold" class="send-icon" />
                  <span>停止生成</span>
                </el-button>
                <el-button
                  v-else
                  type="primary"
                  class="send-btn"
                  :loading="isScanningReceipt"
                  :disabled="(!inputText.trim() && !attachedImage) || isScanningReceipt"
                  @click="handleSend"
                >
                  <Icon v-if="!isScanningReceipt" icon="ph:paper-plane-tilt-fill" class="send-icon" />
                  <span>{{ isScanningReceipt ? '识别中' : '发送' }}</span>
                </el-button>
              </div>
            </div>
          </div>
        </div>
      </main>

      <!-- Panel Overlay Backdrop (Tablet drawer mode) -->
      <div
        v-if="panelOverlay && panelVisible && !isMobile"
        class="panel-overlay-backdrop"
        @click="panelVisible = false"
      ></div>

      <!-- Analysis Result Panel (Desktop & Tablet) -->
      <AnalysisPanel
        v-if="!isMobile"
        :visible="panelVisible"
        :items="panelItems"
        :active-tab-id="activeTabId"
        :overlay="panelOverlay"
        :width="panelWidth"
        @close="panelVisible = false"
        @update:active-tab-id="id => activeTabId = id"
        @remove-tab="handleRemovePanelTab"
        @resize-start="startPanelResize"
        @action-executed="handlePanelActionExecuted"
      />

      <!-- Mobile Bottom Sheet Drawer for Analysis Panel -->
      <el-drawer
        v-if="isMobile"
        v-model="panelVisible"
        direction="btt"
        size="82%"
        :with-header="false"
        class="analysis-mobile-drawer"
        append-to-body
      >
        <div class="mobile-drawer-sheet">
          <div class="drawer-drag-handle-bar" @click="panelVisible = false">
            <div class="drawer-drag-indicator"></div>
          </div>
          <AnalysisPanel
            :visible="true"
            :items="panelItems"
            :active-tab-id="activeTabId"
            :overlay="false"
            width="100%"
            @close="panelVisible = false"
            @update:active-tab-id="id => activeTabId = id"
            @remove-tab="handleRemovePanelTab"
            @action-executed="handlePanelActionExecuted"
          />
        </div>
      </el-drawer>

      <!-- Global Financial Entity Popover -->
      <FinancialEntityPopover
        :visible="popoverVisible"
        :target-rect="popoverTargetRect"
        :detail="popoverDetail"
        :collision="popoverCollision"
        @close="popoverVisible = false"
        @mouseenter="clearPopoverLeaveTimer"
        @mouseleave="handleEntityLeave"
        @navigate="handlePopoverNavigate"
      />
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref, onMounted, onUnmounted, nextTick, watch, computed } from 'vue'
import { useRouter } from 'vue-router'
import { ElMessage, ElMessageBox } from 'element-plus'
import { Icon } from '@iconify/vue'
import { useChatStore } from '@/stores/chat'
import { getMessages } from '@/api/ai'
import { receiptsApi } from '@/api/receipts'
import ChatMessageContent from '@/components/ChatMessageContent.vue'
import PromptStarters from '@/components/PromptStarters.vue'
import WorkflowBar from '@/components/WorkflowBar.vue'
import WorkflowSlashMenu from '@/components/WorkflowSlashMenu.vue'
import AssetMentionMenu, { type MentionItem } from '@/components/AssetMentionMenu.vue'
import FinancialEntityPopover from '@/components/FinancialEntityPopover.vue'
import AnalysisPanel from '@/components/AnalysisPanel.vue'
import { useChatEntityPopover } from '@/composables/chat/useChatEntityPopover'
import { useChatAttachments } from '@/composables/chat/useChatAttachments'
import { useChatAnalysisPanel } from '@/composables/chat/useChatAnalysisPanel'
import { buildSessionMarkdown, buildTranscriptMarkdown, downloadMarkdownFile } from '@/composables/chat/useChatExport'
import { localToday } from '@/utils/format'
import { isMobileMode } from '@/utils/http'
import { isDirectRunWorkflow } from '@/utils/workflow-meta'
import type { WorkflowDef } from '@/types'

const router = useRouter()
const chat = useChatStore()

function goToTraces() {
  if (isMobileMode()) {
    ElMessage.info('请在桌面端查看 AI 审计追踪')
    return
  }
  router.push('/ai-traces')
}

// --- 金融实体悬浮微卡与穿透交互 ---
const {
  loadEntities,
  popoverVisible,
  popoverTargetRect,
  popoverDetail,
  popoverCollision,
  clearPopoverLeaveTimer,
  closePopover,
  handleEntityHover,
  handleEntityLeave,
  handleEntityClick,
  handlePopoverNavigate,
} = useChatEntityPopover({
  onNavigate: (payload) => {
    router.push({
      path: isMobileMode() ? '/m/assets' : '/assets',
      query: payload.type === 'asset' ? { asset_id: payload.id } : { account_id: payload.id },
    })
  },
})

function handlePanelActionExecuted(payload: { messageId?: number; draftToken: string }) {
  const msg = payload.messageId
    ? chat.messages.find((m) => m.id === payload.messageId)
    : chat.messages.find((m) => payload.draftToken && m.content?.includes(payload.draftToken))
  if (msg) {
    handleActionExecuted(msg, { draftToken: payload.draftToken })
  }
}

function handleActionExecuted(msg: any, payload: { draftToken: string }) {
  if (!msg || !payload?.draftToken) return
  const token = payload.draftToken
  if (msg.content && msg.content.includes(token)) {
    msg.content = msg.content
      .replace(/"status"\s*:\s*"pending"/g, '"status": "executed"')
      .replace(/"status"\s*:\s*"proposed"/g, '"status": "executed"')
  }
  if (msg.tool_calls && Array.isArray(msg.tool_calls)) {
    for (const tc of msg.tool_calls) {
      if (tc.result && typeof tc.result === 'string' && tc.result.includes(token)) {
        try {
          const parsed = JSON.parse(tc.result)
          parsed.status = 'executed'
          tc.result = JSON.stringify(parsed)
        } catch {
          // ignore
        }
      } else if (tc.result && typeof tc.result === 'object') {
        const obj = tc.result as Record<string, unknown>
        if (obj.draft_token === token) {
          obj.status = 'executed'
        }
      }
    }
  }
}

// --- 多模态发票/票据截图附件与渐进式工作流 ---
const {
  attachedImage,
  isDragOver,
  isScanningReceipt,
  handlePaste,
  handleDrop,
  handleFileSelect,
  removeAttachment,
  scanReceiptAttachment,
} = useChatAttachments({
  scanReceipt: payload => receiptsApi.scan(payload),
})

const isWorkflowCollapsed = computed(() => {
  return inputText.value.trim().length > 0 || attachedImage.value !== null
})

// --- 顶部会话标题展示与原地重命名 ---
const currentSessionTitle = computed(() => {
  if (!chat.currentSessionId) return '新对话'
  const s = chat.sessions.find(item => item.id === chat.currentSessionId)
  return s?.title?.trim() || '未命名对话'
})

const editingTopTitle = ref(false)
const topRenameText = ref('')

function startTopRename() {
  if (!chat.currentSessionId) return
  topRenameText.value = currentSessionTitle.value
  editingTopTitle.value = true
}

async function saveTopRename() {
  if (!editingTopTitle.value) return
  const next = topRenameText.value.trim()
  editingTopTitle.value = false
  if (next && next !== currentSessionTitle.value && chat.currentSessionId) {
    await chat.renameSession(chat.currentSessionId, next)
    ElMessage.success('会话重命名成功')
  }
}

function cancelTopRename() {
  editingTopTitle.value = false
}

// --- 整场会话累计度量指标统计 ---
const sessionMetrics = computed(() => {
  let totalTokens = 0
  let totalCostUsd = 0
  for (const m of chat.messages) {
    if (m.usage) {
      totalTokens += m.usage.total_tokens || 0
      totalCostUsd += m.usage.cost_usd || 0
    }
  }
  return {
    totalTokens,
    totalCostUsd,
    msgCount: chat.messages.length,
  }
})
const inputText = ref('')
const messagesRef = ref<HTMLElement | null>(null)
const textareaRef = ref<HTMLTextAreaElement | null>(null)
const slashMenuRef = ref<InstanceType<typeof WorkflowSlashMenu> | null>(null)
const showSlashMenu = ref(false)
const slashQuery = ref('')
const mentionMenuRef = ref<InstanceType<typeof AssetMentionMenu> | null>(null)
const showMentionMenu = ref(false)
const mentionQuery = ref('')
const showSidebar = ref(typeof window !== 'undefined' ? window.innerWidth > 1024 : true)
const isSidebarDrawer = ref(typeof window !== 'undefined' ? window.innerWidth <= 1024 : false)
const loadingSessions = ref(false)
const loadingMessages = ref(false)
const editingSessionId = ref<number | null>(null)
const renameTitle = ref('')
const sessionSearch = ref('')
const editingUserIdx = ref<number | null>(null)
const editDraft = ref('')
const DRAFT_PREFIX = 'minefolio:chat:draft:'
const loadMoreTrigger = ref<HTMLElement | null>(null)
let scrollObserver: IntersectionObserver | null = null
const isMobile = ref(typeof window !== 'undefined' ? window.innerWidth < 768 : false)
const inputAreaRef = ref<HTMLElement | null>(null)
const scrollBtnRef = ref<HTMLButtonElement | null>(null)

// --- 分析面板 (Analysis Panel) 状态与拖拽 ---
const {
  panelVisible,
  panelItems,
  activeTabId,
  panelOverlay,
  panelWidth,
  loadPanelWidth,
  startPanelResize,
  checkPanelMode,
  extractedSegmentIdMap,
  handleOpenPanelItem,
  handleRemovePanelTab,
  syncLatestAssistantMessage,
  resetPanel,
} = useChatAnalysisPanel({
  messages: () => chat.messages,
  isMobile,
})

function checkMobile() {
  const wasDrawer = isSidebarDrawer.value
  isSidebarDrawer.value = window.innerWidth <= 1024
  if (!wasDrawer && isSidebarDrawer.value) {
    showSidebar.value = false
  }
  isMobile.value = window.innerWidth < 768
  updateMessagesBottomPadding()
  checkPanelMode()
}

const vFocus = {
  mounted: (el: HTMLElement) => el.focus(),
}

const filteredSessions = computed(() => {
  const q = sessionSearch.value.trim().toLowerCase()
  if (!q) return chat.sessions
  return chat.sessions.filter(s => s.title.toLowerCase().includes(q))
})

/** 智能场景识别微徽章 */
function getSessionTag(title: string): string | null {
  if (!title) return null
  if (/体检|健康|诊断/.test(title)) return '📊'
  if (/记账|消费|支出|调拨/.test(title)) return '🧾'
  if (/行情|汇率|定投|复利/.test(title)) return '📈'
  return null
}

// Group sessions by pinned state and time period
interface SessionGroup { label: string; isPinnedGroup?: boolean; items: typeof chat.sessions }
const groupedSessions = computed<SessionGroup[]>(() => {
  const allFiltered = filteredSessions.value
  const pinnedItems: typeof chat.sessions = []
  const regularItems: typeof chat.sessions = []

  for (const s of allFiltered) {
    if (chat.isSessionPinned(s.id)) {
      pinnedItems.push(s)
    } else {
      regularItems.push(s)
    }
  }

  const result: SessionGroup[] = []
  if (pinnedItems.length > 0) {
    result.push({
      label: '📌 置顶会话',
      isPinnedGroup: true,
      items: pinnedItems,
    })
  }

  const now = Date.now()
  const DAY = 86400000
  const groups: Record<string, typeof chat.sessions> = {}
  const order: string[] = []
  for (const s of regularItems) {
    const t = new Date(s.updated_at).getTime()
    const diff = now - t
    let label: string
    if (diff < DAY) label = '今天'
    else if (diff < 2 * DAY) label = '昨天'
    else if (diff < 7 * DAY) label = '近 7 天'
    else if (diff < 30 * DAY) label = '近 30 天'
    else label = '更早'
    if (!groups[label]) { groups[label] = []; order.push(label) }
    groups[label]!.push(s)
  }

  for (const label of order) {
    result.push({ label, items: groups[label]! })
  }
  return result
})

const exportingSessionId = ref<number | null>(null)

/** 导出单场指定会话为 Markdown 文件 */
async function handleExportSingleSession(s: { id: number; title: string }) {
  if (s.id === chat.currentSessionId && chat.messages.length > 0) {
    exportToMarkdown()
    return
  }

  exportingSessionId.value = s.id
  try {
    const res = await getMessages(s.id, 1, 100)
    const msgs = (res && (res as any).list) || []
    if (msgs.length === 0) {
      ElMessage.info('该会话暂无消息内容')
      return
    }
    const safeTitle = (s.title || 'Minefolio-Chat').replace(/[\\/:*?"<>|]/g, '_')
    downloadMarkdownFile(`${safeTitle}-${localToday()}.md`, buildSessionMarkdown(s.title, msgs))
    ElMessage.success(`会话「${s.title || 'Minefolio-Chat'}」已导出为 Markdown 文件！`)
  } catch {
    ElMessage.error('导出失败，请稍后重试')
  } finally {
    exportingSessionId.value = null
  }
}

function draftKey(): string {
  return `${DRAFT_PREFIX}${chat.currentSessionId ?? 'new'}`
}
function loadDraft() {
  try {
    const v = localStorage.getItem(draftKey())
    if (v !== null) inputText.value = v
    else inputText.value = ''
  } catch { /* ignore */ }
  nextTick(() => adjustTextareaHeight())
}
function persistDraft() {
  try { localStorage.setItem(draftKey(), inputText.value) } catch { /* ignore */ }
}

function formatChatMarkdown(): string {
  const session = chat.sessions.find(s => s.id === chat.currentSessionId)
  return buildTranscriptMarkdown({
    title: session?.title,
    sessionId: chat.currentSessionId,
    model: chat.currentModel,
    provider: chat.currentProvider,
    messages: chat.messages,
  })
}

function exportToMarkdown() {
  if (chat.messages.length === 0) {
    ElMessage.info('当前没有可导出的对话内容')
    return
  }
  downloadMarkdownFile(`Minefolio-AI-Chat-${localToday()}.md`, formatChatMarkdown())
  ElMessage.success('对话记录已导出为 Markdown 文件！')
}

async function copyAllChat() {
  if (chat.messages.length === 0) {
    ElMessage.info('当前没有可复制的内容')
    return
  }
  const md = formatChatMarkdown()
  try {
    await navigator.clipboard.writeText(md)
    ElMessage.success('已复制整场对话记录至剪贴板')
  } catch {
    ElMessage.error('复制失败，请手动选择复制')
  }
}

function formatTime(dateStr?: string): string {
  if (!dateStr) return ''
  try {
    const d = new Date(dateStr)
    if (isNaN(d.getTime())) return ''
    const now = new Date()
    const diff = now.getTime() - d.getTime()
    if (diff < 60000) return '刚刚'
    if (diff < 3600000) return `${Math.floor(diff / 60000)}分钟前`
    if (diff < 86400000) return `${Math.floor(diff / 3600000)}小时前`
    return d.toLocaleDateString()
  } catch { return '' }
}

interface FlatModelOption {
  provider_id: string
  provider_name: string
  model: string
  value: string
  label: string
}

const modelOptions = computed<FlatModelOption[]>(() => {
  const list: FlatModelOption[] = []
  chat.availableModels.forEach((p) => {
    const models = p.models && p.models.length > 0 ? p.models : []
    models.forEach((m) => {
      list.push({
        provider_id: p.provider_id,
        provider_name: p.provider_name || p.provider_id,
        model: m,
        value: `${p.provider_id}/${m}`,
        label: `${p.provider_name || p.provider_id} (${m})`,
      })
    })
  })
  if (chat.settings?.default_provider && chat.settings?.default_model) {
    const defKey = `${chat.settings.default_provider}/${chat.settings.default_model}`
    if (!list.some(item => item.value === defKey)) {
      list.unshift({
        provider_id: chat.settings.default_provider,
        provider_name: chat.settings.default_provider,
        model: chat.settings.default_model,
        value: defKey,
        label: `${chat.settings.default_provider} (${chat.settings.default_model})`,
      })
    }
  }
  return list
})

const selectedModelKey = computed({
  get() {
    if (chat.currentProvider && chat.currentModel) {
      return `${chat.currentProvider}/${chat.currentModel}`
    }
    if (chat.settings?.default_provider && chat.settings?.default_model) {
      return `${chat.settings.default_provider}/${chat.settings.default_model}`
    }
    if (modelOptions.value.length > 0) {
      return modelOptions.value[0]!.value
    }
    return ''
  },
  set(val: string) {
    const parts = val.split('/')
    if (parts.length === 2) {
      chat.setModel(parts[1]!, parts[0]!)
    } else {
      chat.setModel(val)
    }
  },
})

const currentModelDisplay = computed(() => {
  const currentKey = selectedModelKey.value
  const opt = modelOptions.value.find(o => o.value === currentKey)
  if (!opt) return '选择模型'
  return opt.model || opt.label
})

const currentModelProvider = computed(() => {
  const currentKey = selectedModelKey.value
  const opt = modelOptions.value.find(o => o.value === currentKey)
  return opt?.provider_name || ''
})

const autoStickToBottom = ref(true)
const showScrollBottomBtn = ref(false)
let scrollTicking = false
let scrollRafId: number | null = null
let isProgrammaticScroll = false
let lastMsgEl: HTMLElement | null = null
let lastMsgResizeObserver: ResizeObserver | null = null
let containerResizeObserver: ResizeObserver | null = null

/**
 * 滚动到底部
 * @param smooth 是否平滑滚动。流式输出期间必须为 false (直达最底端)，
 * 防止高频增量 token 与 smooth 滚动动画冲突积压引发的画面掉帧与脱节
 */
function scrollToBottom(smooth = false) {
  const el = messagesRef.value
  if (!el) return
  if (chat.messages.length === 0) {
    el.scrollTop = 0
    return
  }
  if (!autoStickToBottom.value) return
  if (scrollRafId !== null) return

  scrollRafId = requestAnimationFrame(() => {
    scrollRafId = null
    const targetEl = messagesRef.value
    if (!targetEl || !autoStickToBottom.value) return

    isProgrammaticScroll = true
    if (smooth && typeof targetEl.scrollTo === 'function') {
      targetEl.scrollTo({ top: targetEl.scrollHeight, behavior: 'smooth' })
    } else {
      targetEl.scrollTop = targetEl.scrollHeight
    }

    // 延后一帧复位程序化滚动标志，吸收由自身设置 scrollTop 触发的 scroll 事件，避免误触判定
    requestAnimationFrame(() => {
      isProgrammaticScroll = false
    })
  })
}

/**
 * 用户主动点击“回到底部”悬浮按钮：平滑滑至底端并重新激活吸底跟随
 */
function forceScrollToBottom() {
  const el = messagesRef.value
  if (!el) return
  autoStickToBottom.value = true
  showScrollBottomBtn.value = false
  if (scrollRafId !== null) {
    cancelAnimationFrame(scrollRafId)
    scrollRafId = null
  }
  isProgrammaticScroll = true
  if (typeof el.scrollTo === 'function') {
    el.scrollTo({ top: el.scrollHeight, behavior: 'smooth' })
  } else {
    el.scrollTop = el.scrollHeight
  }
  setTimeout(() => {
    isProgrammaticScroll = false
  }, 400)
}

/**
 * 滚动事件处理：
 * 1. 距离底部 <= 40px 时：认定用户已浏览回底端，自动恢复 autoStickToBottom 并隐藏按钮
 * 2. 距离底部 > 100px 时：展现“回到底部”悬浮按钮；若非程序化写入且非吸底锁定状态，暂停自动滚动
 */
function handleScroll() {
  closePopover()
  if (scrollTicking) return
  scrollTicking = true
  requestAnimationFrame(() => {
    scrollTicking = false
    const el = messagesRef.value
    if (!el) return
    const distance = el.scrollHeight - el.scrollTop - el.clientHeight

    if (distance <= 40) {
      autoStickToBottom.value = true
      showScrollBottomBtn.value = false
      return
    }

    if (distance > 100) {
      showScrollBottomBtn.value = true
      if (!isProgrammaticScroll) {
        autoStickToBottom.value = false
      }
    }
  })
}

/**
 * 滚轮事件拦截：用户主动向上滚动时，立即解除吸底跟随并显示“回到底部”按钮
 */
function handleWheel(e: WheelEvent) {
  if (e.deltaY < -2) {
    autoStickToBottom.value = false
    showScrollBottomBtn.value = true
  }
}

/**
 * 移动端触控事件拦截：手指下拉（内容向上滚动翻看历史）时，立即解除吸底
 */
let touchStartY = 0
function handleTouchStart(e: TouchEvent) {
  if (e.touches[0]) {
    touchStartY = e.touches[0].clientY
  }
}
function handleTouchMove(e: TouchEvent) {
  if (!e.touches[0]) return
  const deltaY = e.touches[0].clientY - touchStartY
  if (deltaY > 12) {
    autoStickToBottom.value = false
    showScrollBottomBtn.value = true
  }
}

/**
 * 动态挂载并监听最新一条消息（流式生成行）的 ResizeObserver：
 * 只要最新输出产生高度增长（打字机逐字输出、思考过程膨胀、工具卡片展开、代码/图表/表格渲染），
 * 且处于跟随状态，立即将视口牢牢锁在最新内容最底端
 */
function setupLastMsgObserver(el: HTMLElement | null) {
  if (lastMsgResizeObserver) {
    lastMsgResizeObserver.disconnect()
    lastMsgResizeObserver = null
  }
  lastMsgEl = el
  if (!el || typeof ResizeObserver === 'undefined') return
  lastMsgResizeObserver = new ResizeObserver(() => {
    if (autoStickToBottom.value) {
      scrollToBottom(false)
    }
  })
  lastMsgResizeObserver.observe(el)
}

function onLastMessageRef(el: HTMLElement | null) {
  if (el !== lastMsgEl) {
    setupLastMsgObserver(el)
  }
}

let inputAreaObserver: ResizeObserver | null = null

// 输入框已通过 Flex 纵向底栏与消息流解耦，天然不会遮挡最后一条消息。
// 监听输入区域高度变化时，如果用户处于底部跟随状态，保持滚动锁定在最底端。
function updateMessagesBottomPadding() {
  if (autoStickToBottom.value && messagesRef.value) {
    scrollToBottom(false)
  }
}

function setupInputAreaObserver() {
  inputAreaObserver?.disconnect()
  inputAreaObserver = null
  const area = inputAreaRef.value
  if (!area || typeof ResizeObserver === 'undefined') return
  inputAreaObserver = new ResizeObserver(updateMessagesBottomPadding)
  inputAreaObserver.observe(area)
  updateMessagesBottomPadding()
}

/**
 * 监听消息容器自身视口尺寸变化（窗口 resize、软键盘弹出/收起、侧边栏折叠/展开等）：
 * 保持底部内容锁定在可视区域
 */
function setupContainerResizeObserver() {
  containerResizeObserver?.disconnect()
  containerResizeObserver = null
  const el = messagesRef.value
  if (!el || typeof ResizeObserver === 'undefined') return
  containerResizeObserver = new ResizeObserver(() => {
    if (autoStickToBottom.value) {
      scrollToBottom(false)
    }
  })
  containerResizeObserver.observe(el)
}

// 监听最后一条消息的全部流式动态字段：正文增量、思考推理链、工具调用状态、工作流步骤与进度、错误与统计
const lastMsgStreamTrack = computed(() => {
  const msgs = chat.messages
  if (msgs.length === 0) return ''
  const m = msgs[msgs.length - 1]
  if (!m) return ''
  return [
    m.id,
    m.role,
    m.content || '',
    m.thinking_content || '',
    m.tool_calls?.length || 0,
    m.tool_calls?.[m.tool_calls.length - 1]?.result || '',
    m.workflowData?.status || '',
    m.workflowData?.progress || 0,
    m.workflowData?.steps?.length || 0,
    m.error || '',
    m.usage ? 1 : 0,
  ].join('|')
})

watch(lastMsgStreamTrack, () => {
  if (autoStickToBottom.value) {
    scrollToBottom(false)
  }
})

onUnmounted(() => {
  if (scrollRafId !== null) cancelAnimationFrame(scrollRafId)
  if (lastMsgResizeObserver) { lastMsgResizeObserver.disconnect(); lastMsgResizeObserver = null }
  if (containerResizeObserver) { containerResizeObserver.disconnect(); containerResizeObserver = null }
})



function handleInput() {
  adjustTextareaHeight()
  const text = inputText.value
  const cursor = textareaRef.value?.selectionStart ?? text.length
  const textBeforeCursor = text.slice(0, cursor)

  const slashMatch = textBeforeCursor.match(/(?:^|\s)\/([^\s/]*)$/)
  if (slashMatch) {
    showSlashMenu.value = true
    slashQuery.value = slashMatch[1] ?? ''
    showMentionMenu.value = false
    return
  } else {
    showSlashMenu.value = false
    slashQuery.value = ''
  }

  const mentionMatch = textBeforeCursor.match(/(?:^|\s)@([^\s@]*)$/)
  if (mentionMatch) {
    showMentionMenu.value = true
    mentionQuery.value = mentionMatch[1] ?? ''
  } else {
    showMentionMenu.value = false
    mentionQuery.value = ''
  }
}

function handleTextareaKeyDown(e: KeyboardEvent) {
  if (showSlashMenu.value && slashMenuRef.value) {
    const handled = slashMenuRef.value.handleKeyDown(e)
    if (handled) return
  }
  if (showMentionMenu.value && mentionMenuRef.value) {
    const handled = mentionMenuRef.value.handleKeyDown(e)
    if (handled) return
  }
  if (e.key === 'Enter' && !e.shiftKey) {
    e.preventDefault()
    handleSend()
  } else if (e.key === 'Escape') {
    if (showSlashMenu.value) {
      showSlashMenu.value = false
    } else if (showMentionMenu.value) {
      showMentionMenu.value = false
    } else {
      handleStop()
    }
  }
}

function handleSelectMention(item: MentionItem) {
  showMentionMenu.value = false
  const text = inputText.value
  const cursor = textareaRef.value?.selectionStart ?? text.length
  const textBeforeCursor = text.slice(0, cursor)
  const atIdx = textBeforeCursor.lastIndexOf('@')
  if (atIdx >= 0) {
    const replacement = `[@${item.name}] `
    inputText.value = text.slice(0, atIdx) + replacement + text.slice(cursor)
    nextTick(() => {
      adjustTextareaHeight()
      const nextCursor = atIdx + replacement.length
      textareaRef.value?.setSelectionRange(nextCursor, nextCursor)
      textareaRef.value?.focus()
    })
  }
}

interface FollowupChip {
  text: string
  icon?: string
}

const followUpSuggestions = computed<FollowupChip[]>(() => {
  if (chat.isStreaming || chat.messages.length === 0) return []
  const lastMsg = chat.messages[chat.messages.length - 1]
  if (!lastMsg || lastMsg.role !== 'assistant' || (!lastMsg.content && !lastMsg.tool_calls?.length)) {
    return []
  }

  const content = (lastMsg.content || '').toLowerCase()
  const tools = (lastMsg.tool_calls || []).map(t => t.name)
  const chips: FollowupChip[] = []

  // Case 1: Expense / spending analysis
  if (content.includes('支出') || content.includes('消费') || content.includes('超支') || tools.includes('get_daily_expenses') || tools.includes('get_expense_by_category')) {
    chips.push({ text: '查看超支商户与大额支出排行', icon: 'ph:ranking-bold' })
    chips.push({ text: '生成近30天收支环比对比图', icon: 'ph:chart-bar-bold' })
    chips.push({ text: '针对超支项目给出预算削减建议', icon: 'ph:lightbulb-bold' })
  }
  // Case 2: Asset / portfolio / investment
  else if (content.includes('资产') || content.includes('持仓') || content.includes('投资') || content.includes('收益') || tools.includes('get_assets') || tools.includes('analyze_financial_health')) {
    chips.push({ text: '评估当前资产配置风险与回撤', icon: 'ph:shield-check-bold' })
    chips.push({ text: '分析不同资产大类的收益贡献', icon: 'ph:chart-pie-bold' })
    chips.push({ text: '如何进行下一阶段的组合再平衡？', icon: 'ph:git-merge-bold' })
  }
  // Case 3: Transfer / bookkeeping proposal
  else if (tools.includes('propose_daily_expense') || tools.includes('propose_transfer') || content.includes('记账') || content.includes('转账')) {
    chips.push({ text: '查看今日已入账的所有交易明细', icon: 'ph:receipt-bold' })
    chips.push({ text: '核对本月预算剩余额度', icon: 'ph:wallet-bold' })
  }
  // Case 4: General / Fallback finance followups
  else {
    chips.push({ text: '深入分析背后的核心财务原因', icon: 'ph:magnifying-glass-plus-bold' })
    chips.push({ text: '将该建议总结并转化为执行清单', icon: 'ph:check-square-bold' })
    chips.push({ text: '评估该方案对我现金流的影响', icon: 'ph:currency-cny-bold' })
  }

  return chips.slice(0, 3)
})

function handleSelectFollowup(chip: FollowupChip) {
  inputText.value = chip.text
  nextTick(() => {
    handleSend()
  })
}

function handleSelectWorkflow(wf: WorkflowDef) {
  showSlashMenu.value = false
  const text = inputText.value
  const cursor = textareaRef.value?.selectionStart ?? text.length
  const textBeforeCursor = text.slice(0, cursor)
  const slashIdx = textBeforeCursor.lastIndexOf('/')
  if (slashIdx >= 0) {
    inputText.value = (text.slice(0, slashIdx) + text.slice(cursor)).trim()
    nextTick(() => adjustTextareaHeight())
  }
  if (isDirectRunWorkflow(wf.id)) {
    chat.runWorkflow(wf.id)
  } else {
    chat.stageWorkflow(wf.id)
  }
}

function adjustTextareaHeight() {
  if (textareaRef.value) {
    textareaRef.value.style.height = 'auto'
    const nextH = Math.min(Math.max(textareaRef.value.scrollHeight, 44), 160)
    textareaRef.value.style.height = `${nextH}px`
  }
}

async function copyText(text: string) {
  try {
    await navigator.clipboard.writeText(text)
    ElMessage.success('已复制到剪贴板')
  } catch {
    ElMessage.error('复制失败，请手动复制')
  }
}

async function fetchSessions() {
  loadingSessions.value = true
  try { await chat.fetchSessions() }
  catch { ElMessage.error('加载对话列表失败') }
  finally { loadingSessions.value = false }
}

async function fetchModels() {
  try {
    await chat.fetchModels()
  } catch { /* optional */ }
}

async function fetchSettings() {
  try { await chat.fetchSettings() } catch { /* optional */ }
}

async function selectSession(id: number) {
  loadingMessages.value = true
  await chat.selectSession(id)
  loadingMessages.value = false
  autoStickToBottom.value = true
  showScrollBottomBtn.value = false
  nextTick(() => scrollToBottom(false))
}

function handleNewChat() {
  chat.clearCurrentSession()
  inputText.value = ''
  adjustTextareaHeight()
  nextTick(() => {
    if (messagesRef.value) messagesRef.value.scrollTop = 0
  })
}

async function handleClearChat() {
  try {
    await ElMessageBox.confirm('确定清空当前对话中的消息？', '提示', { type: 'warning' })
    chat.clearMessages()
  } catch { /* cancelled */ }
}

function startRename(s: { id: number; title: string }) {
  editingSessionId.value = s.id
  renameTitle.value = s.title
}
async function saveRename(id: number) {
  if (editingSessionId.value === null) return
  const title = renameTitle.value.trim()
  editingSessionId.value = null
  if (title) {
    await chat.renameSession(id, title)
    ElMessage.success('会话重命名成功')
  }
}

function cancelRename() {
  editingSessionId.value = null
}

async function handleDeleteSession(id: number) {
  try {
    await ElMessageBox.confirm('确定删除此会话记录？', '提示', { type: 'warning' })
    await chat.deleteSessionById(id)
    ElMessage.success('会话已删除')
  } catch { /* cancelled */ }
}

function handleQuickPrompt(promptText: string) {
  inputText.value = promptText
  handleSend()
}

async function handleSend() {
  if (chat.isStreaming || isScanningReceipt.value) return
  const rawText = inputText.value.trim()
  if (rawText.length > 2000) {
    ElMessage.warning('输入内容不能超过 2000 字')
    return
  }
  const img = attachedImage.value
  if (!rawText && !img) return

  inputText.value = ''
  attachedImage.value = null
  adjustTextareaHeight()
  autoStickToBottom.value = true
  showScrollBottomBtn.value = false
  nextTick(() => scrollToBottom(false))

  const finalPrompt = img ? await scanReceiptAttachment(img, rawText) : rawText

  try {
    await chat.sendMessage(finalPrompt)
  } catch (err) {
    console.error('[chat] sendMessage failed', err)
  }
  nextTick(() => scrollToBottom(false))
  fetchSessions()
}

async function handleRegenerate() {
  if (chat.isStreaming) return
  autoStickToBottom.value = true
  showScrollBottomBtn.value = false
  await chat.regenerateLastMessage()
  nextTick(() => scrollToBottom(false))
}

function handleStop() {
  if (!chat.isStreaming) return
  chat.abortCurrentStream()
  if (autoStickToBottom.value) {
    nextTick(() => scrollToBottom(false))
  }
}

function startEdit(idx: number, content: string) {
  if (chat.isStreaming) return
  editingUserIdx.value = idx
  editDraft.value = content
}

function cancelEdit() {
  editingUserIdx.value = null
  editDraft.value = ''
}

async function confirmEdit(idx: number) {
  const text = editDraft.value.trim()
  if (!text || chat.isStreaming) return
  editingUserIdx.value = null
  editDraft.value = ''
  autoStickToBottom.value = true
  showScrollBottomBtn.value = false
  await chat.editAndResend(idx, text)
  nextTick(() => scrollToBottom(false))
  fetchSessions()
}

async function handleRetryMessage(msgId: number) {
  const idx = chat.messages.findIndex(m => m.id === msgId)
  if (idx < 0) return
  // 取该失败消息之前最近的一条用户消息（而非会话首条），避免重发陈旧 prompt 并误截断整段
  let userIdx = -1
  for (let i = idx - 1; i >= 0; i--) {
    if (chat.messages[i]?.role === 'user') { userIdx = i; break }
  }
  if (userIdx < 0) { ElMessage.error('无法找到对应用户消息') }
  else {
    const userMsg = chat.messages[userIdx]!
    await chat.editAndResend(userIdx, userMsg.content)
    nextTick(() => scrollToBottom(false))
  }
}

function setupInfiniteScroll() {
  if (scrollObserver) { scrollObserver.disconnect() }
  if (!loadMoreTrigger.value || typeof IntersectionObserver === 'undefined') return
  scrollObserver = new IntersectionObserver((entries) => {
    if (entries[0]?.isIntersecting && !chat.loadingMoreMessages && chat.loadedMessagePage * 50 < (chat.messageTotal ?? 0)) {
      chat.loadMoreMessages()
    }
  }, { root: messagesRef.value, threshold: 0.1 })
  scrollObserver.observe(loadMoreTrigger.value)
}

// 仅当“末尾新增消息”（最后一条消息 id 变化）时才自动吸底并滚动到底部。
// 向上滚动加载的历史消息是前置插入的，末尾 id 不变，此时绝不能把用户拽回底部。
const lastMessageId = computed(() => {
  const msgs = chat.messages
  return msgs.length > 0 ? (msgs[msgs.length - 1]?.id ?? null) : null
})

watch(lastMessageId, (id, prevId) => {
  if (id === null || id === prevId) return
  autoStickToBottom.value = true
  nextTick(() => scrollToBottom(false))
})

watch(inputText, () => { persistDraft() })
watch(() => chat.currentSessionId, () => {
  resetPanel()
  loadDraft()
  nextTick(() => setupInfiniteScroll())
})

// 流式生成结束时，自动扫描助理最新输出并弹出展示，同时稳固吸底
watch(() => chat.isStreaming, (streaming, prevStreaming) => {
  if (!streaming && prevStreaming) {
    if (autoStickToBottom.value) {
      nextTick(() => {
        scrollToBottom(false)
        setTimeout(() => {
          if (autoStickToBottom.value) scrollToBottom(false)
        }, 80)
      })
    }
    const msgs = chat.messages
    if (msgs.length > 0) {
      syncLatestAssistantMessage(msgs[msgs.length - 1])
    }
  }
})

function onGlobalKeydown(e: KeyboardEvent) {
  if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === 'k') {
    if (isMobileMode()) {
      e.preventDefault()
      textareaRef.value?.focus()
    }
  } else if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === 'n') {
    e.preventDefault()
    handleNewChat()
  } else if (e.key === 'Escape' && chat.isStreaming) {
    handleStop()
  }
}

let isUnmounted = false

onMounted(async () => {
  loadPanelWidth()
  checkMobile()
  window.addEventListener('resize', checkMobile)
  window.addEventListener('keydown', onGlobalKeydown)
  loadEntities()
  await Promise.allSettled([fetchSessions(), fetchSettings(), fetchModels()])
  if (isUnmounted) return
  if (chat.settings?.default_provider) {
    chat.setModel(chat.currentModel, chat.settings.default_provider)
  }
  if (chat.settings?.default_model) {
    chat.setModel(chat.settings.default_model, chat.currentProvider)
  } else if (!chat.currentModel && modelOptions.value.length > 0) {
    chat.setModel(modelOptions.value[0]!.model, modelOptions.value[0]!.provider_id)
  }
  if (!chat.currentSessionId) handleNewChat()
  loadDraft()
  nextTick(() => {
    if (isUnmounted) return
    scrollToBottom(false)
    setupInfiniteScroll()
    setupInputAreaObserver()
    setupContainerResizeObserver()
    textareaRef.value?.focus()
  })
})

onUnmounted(() => {
  isUnmounted = true
  window.removeEventListener('keydown', onGlobalKeydown)
  window.removeEventListener('resize', checkMobile)
  if (scrollObserver) { scrollObserver.disconnect(); scrollObserver = null }
  if (inputAreaObserver) { inputAreaObserver.disconnect(); inputAreaObserver = null }
  chat.abortCurrentStream()
})
</script>

<style scoped src="@/styles/chat-page.css"></style>

<style scoped>
.message-row.streaming-active-row,
.message-row:last-child {
  content-visibility: visible;
  contain-intrinsic-size: auto;
}
</style>
