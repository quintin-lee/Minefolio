<template>
  <div class="fx-skeleton-container">
    <!-- 1. FX Rate Cards Skeleton -->
    <div class="fx-cards-skeleton">
      <div class="section-header-placeholder">
        <span class="mf-skeleton-block" style="width: 140px; height: 20px;" />
        <span class="mf-skeleton-block" style="width: 180px; height: 14px;" />
      </div>
      <div class="cards-grid">
        <div v-for="i in 8" :key="i" class="rate-skeleton-card mf-skeleton-card">
          <div class="card-top">
            <div class="currency-info">
              <span class="mf-skeleton-block flag-placeholder" />
              <span class="mf-skeleton-block" style="width: 40px; height: 16px;" />
              <span class="mf-skeleton-block" style="width: 50px; height: 14px;" />
            </div>
            <span class="mf-skeleton-block" style="width: 55px; height: 18px; border-radius: 4px;" />
          </div>
          <div class="card-rate">
            <span class="mf-skeleton-block" style="width: 100px; height: 26px;" />
          </div>
          <div class="card-footer">
            <span class="mf-skeleton-block" style="width: 80px; height: 12px;" />
            <span class="mf-skeleton-block" style="width: 80px; height: 12px;" />
          </div>
        </div>
      </div>
    </div>

    <!-- 2. Middle Split Row Skeleton (Converter & Trend) -->
    <div class="middle-split-row">
      <div class="converter-skeleton-card mf-skeleton-card">
        <div class="card-header-placeholder">
          <span class="mf-skeleton-block" style="width: 120px; height: 18px;" />
          <span class="mf-skeleton-block" style="width: 80px; height: 14px;" />
        </div>
        <div class="converter-body-skeleton">
          <span class="mf-skeleton-block" style="width: 60px; height: 12px;" />
          <span class="mf-skeleton-block" style="width: 100%; height: 36px; border-radius: 8px;" />
          <div style="text-align: center; margin: 4px 0;">
            <span class="mf-skeleton-block" style="width: 32px; height: 32px; border-radius: 50%;" />
          </div>
          <span class="mf-skeleton-block" style="width: 60px; height: 12px;" />
          <span class="mf-skeleton-block" style="width: 100%; height: 36px; border-radius: 8px;" />
        </div>
      </div>

      <div class="trend-skeleton-card mf-skeleton-card">
        <div class="card-header-placeholder">
          <span class="mf-skeleton-block" style="width: 140px; height: 18px;" />
          <span class="mf-skeleton-block" style="width: 150px; height: 26px; border-radius: 6px;" />
        </div>
        <div class="trend-stats-placeholder">
          <span v-for="i in 4" :key="i" class="mf-skeleton-block" style="width: 70px; height: 34px; border-radius: 6px;" />
        </div>
        <div class="chart-canvas-placeholder">
          <span class="mf-skeleton-block" style="width: 100%; height: 220px; border-radius: 8px;" />
        </div>
      </div>
    </div>

    <!-- 3. Holdings Summary Skeleton -->
    <div class="holdings-skeleton-card mf-skeleton-card">
      <div class="card-header-placeholder">
        <span class="mf-skeleton-block" style="width: 150px; height: 18px;" />
        <span class="mf-skeleton-block" style="width: 200px; height: 14px;" />
      </div>
      <div class="metrics-grid">
        <div v-for="i in 3" :key="i" class="metric-skeleton-box">
          <span class="mf-skeleton-block" style="width: 70px; height: 12px; margin-bottom: 6px;" />
          <span class="mf-skeleton-block" style="width: 110px; height: 22px;" />
        </div>
      </div>
    </div>
  </div>
</template>

<script setup lang="ts">
// FinTech Glassmorphic Skeleton for Real-time FX & Currency
</script>

<style scoped>
.fx-skeleton-container {
  display: flex;
  flex-direction: column;
  gap: 20px;
}

.section-header-placeholder {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 12px;
}

.cards-grid {
  display: grid;
  grid-template-columns: repeat(4, 1fr);
  gap: 12px;
}

@media (max-width: 1200px) {
  .cards-grid {
    grid-template-columns: repeat(2, 1fr);
  }
}

.rate-skeleton-card {
  padding: 14px;
}

.card-top {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 10px;
}

.currency-info {
  display: flex;
  align-items: center;
  gap: 6px;
}

.flag-placeholder {
  width: 18px;
  height: 18px;
  border-radius: 50%;
}

.card-rate {
  margin-bottom: 10px;
}

.card-footer {
  display: flex;
  justify-content: space-between;
}

.middle-split-row {
  display: grid;
  grid-template-columns: 1fr 1.5fr;
  gap: 16px;
}

@media (max-width: 1024px) {
  .middle-split-row {
    grid-template-columns: 1fr;
  }
}

.converter-skeleton-card,
.trend-skeleton-card,
.holdings-skeleton-card {
  padding: 18px;
}

.card-header-placeholder {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 16px;
}

.converter-body-skeleton {
  display: flex;
  flex-direction: column;
  gap: 10px;
}

.trend-stats-placeholder {
  display: flex;
  justify-content: space-between;
  gap: 10px;
  margin-bottom: 14px;
}

.metrics-grid {
  display: grid;
  grid-template-columns: repeat(3, 1fr);
  gap: 12px;
}

.metric-skeleton-box {
  background: var(--mf-surface-muted);
  border: 1px solid var(--mf-border-subtle);
  padding: 14px 16px;
  border-radius: var(--mf-radius-md);
  display: flex;
  flex-direction: column;
}
</style>
