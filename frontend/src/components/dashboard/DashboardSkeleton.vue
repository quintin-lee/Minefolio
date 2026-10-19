<template>
  <div class="dashboard-skeleton-container">
    <!-- 1. Top Bento Grid Skeletons -->
    <el-row :gutter="16" class="bento-top-row">
      <!-- Left: Hero Net Worth Card Skeleton -->
      <el-col :xs="24" :sm="24" :md="14" class="bento-col">
        <div class="hero-skeleton-card mf-skeleton-card">
          <div class="hero-top">
            <div class="hero-label-wrap">
              <span class="mf-skeleton-block" style="width: 80px; height: 16px; border-radius: 4px;" />
              <span class="mf-skeleton-block" style="width: 70px; height: 14px;" />
            </div>
            <span class="mf-skeleton-block" style="width: 65px; height: 22px; border-radius: 12px;" />
          </div>

          <div class="hero-amount-row">
            <span class="mf-skeleton-block" style="width: 220px; height: 42px; border-radius: 6px;" />
            <span class="mf-skeleton-block hero-sparkline-placeholder" />
          </div>

          <div class="hero-bottom-stats">
            <span class="mf-skeleton-block" style="width: 140px; height: 16px;" />
            <span class="mf-skeleton-block" style="width: 110px; height: 16px;" />
          </div>
        </div>
      </el-col>

      <!-- Right: 3 Secondary Cards Skeletons -->
      <el-col :xs="24" :sm="24" :md="10" class="bento-col">
        <div class="secondary-grid">
          <div v-for="i in 3" :key="i" class="secondary-skeleton-card mf-skeleton-card">
            <div class="sec-top">
              <span class="mf-skeleton-block" style="width: 60px; height: 14px;" />
              <span class="mf-skeleton-block" style="width: 24px; height: 24px; border-radius: 6px;" />
            </div>
            <span class="mf-skeleton-block" style="width: 130px; height: 26px; margin: 8px 0 6px 0;" />
            <span class="mf-skeleton-block" style="width: 80px; height: 12px;" />
          </div>
        </div>
      </el-col>
    </el-row>

    <!-- 2. Bottom Charts Row Skeletons -->
    <el-row :gutter="16" class="charts-row">
      <!-- Net Worth Chart Card Skeleton -->
      <el-col :xs="24" :sm="24" :md="14" class="chart-col">
        <div class="chart-skeleton-card mf-skeleton-card">
          <div class="card-header-placeholder">
            <span class="mf-skeleton-block" style="width: 120px; height: 18px;" />
            <span class="mf-skeleton-block" style="width: 160px; height: 24px; border-radius: 6px;" />
          </div>
          <div class="chart-canvas-placeholder">
            <span class="mf-skeleton-block" style="width: 100%; height: 280px; border-radius: 8px;" />
          </div>
        </div>
      </el-col>

      <!-- Asset Allocation Pie Skeleton -->
      <el-col :xs="24" :sm="24" :md="10" class="chart-col">
        <div class="chart-skeleton-card mf-skeleton-card">
          <div class="card-header-placeholder">
            <span class="mf-skeleton-block" style="width: 100px; height: 18px;" />
            <span class="mf-skeleton-block" style="width: 32px; height: 16px;" />
          </div>
          <div class="pie-placeholder-wrap">
            <span class="mf-skeleton-block pie-circle-placeholder" />
          </div>
        </div>
      </el-col>
    </el-row>
  </div>
</template>

<script setup lang="ts">
// FinTech Glassmorphic Skeleton for Bento Dashboard
</script>

<style scoped>
.dashboard-skeleton-container {
  display: flex;
  flex-direction: column;
  gap: 16px;
}

.bento-top-row {
  margin-bottom: 4px;
}

.bento-col {
  margin-bottom: 12px;
}

.hero-skeleton-card {
  padding: 24px 28px;
  min-height: 200px;
  display: flex;
  flex-direction: column;
  justify-content: space-between;
}

.hero-top {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 16px;
}

.hero-label-wrap {
  display: flex;
  align-items: center;
  gap: 10px;
}

.hero-amount-row {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 20px;
}

.hero-sparkline-placeholder {
  width: 130px;
  height: 38px;
  border-radius: 6px;
}

.hero-bottom-stats {
  display: flex;
  gap: 24px;
}

.secondary-grid {
  display: grid;
  grid-template-columns: 1fr;
  gap: 10px;
}

.secondary-skeleton-card {
  padding: 14px 18px;
}

.sec-top {
  display: flex;
  justify-content: space-between;
  align-items: center;
}

.charts-row {
  margin-top: 4px;
}

.chart-col {
  margin-bottom: 16px;
}

.chart-skeleton-card {
  padding: 16px 20px;
  min-height: 340px;
}

.card-header-placeholder {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 20px;
}

.pie-placeholder-wrap {
  display: flex;
  justify-content: center;
  align-items: center;
  height: 270px;
}

.pie-circle-placeholder {
  width: 200px;
  height: 200px;
  border-radius: 50%;
}
</style>
