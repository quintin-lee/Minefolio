<template>
  <div class="market-skeleton-container">
    <!-- 1. Clocks Ribbon Skeleton -->
    <div class="skeleton-ribbon mf-skeleton-card">
      <div class="ribbon-left">
        <span class="mf-skeleton-block" style="width: 120px; height: 16px;" />
        <div class="pills-row">
          <span v-for="i in 5" :key="i" class="mf-skeleton-block skeleton-pill" />
        </div>
      </div>
      <div class="ribbon-right">
        <span class="mf-skeleton-block" style="width: 60px; height: 14px;" />
        <span class="mf-skeleton-block" style="width: 80px; height: 24px; border-radius: 6px;" />
        <span class="mf-skeleton-block" style="width: 65px; height: 24px; border-radius: 6px;" />
      </div>
    </div>

    <!-- 2. Filter Bar Skeleton -->
    <div class="skeleton-filter-bar">
      <div class="category-pills">
        <span v-for="i in 6" :key="i" class="mf-skeleton-block skeleton-filter-pill" />
      </div>
      <span class="mf-skeleton-block" style="width: 240px; height: 32px; border-radius: var(--mf-radius-md);" />
    </div>

    <!-- 3. Cards Grid Skeleton (8 Cards) -->
    <div class="indices-grid">
      <div v-for="i in 8" :key="i" class="index-skeleton-card mf-skeleton-card">
        <div class="card-top">
          <div class="name-box">
            <span class="mf-skeleton-block flag-placeholder" />
            <span class="mf-skeleton-block" style="width: 80px; height: 16px;" />
          </div>
          <span class="mf-skeleton-block" style="width: 50px; height: 12px;" />
        </div>

        <div class="card-price-row">
          <span class="mf-skeleton-block" style="width: 100px; height: 26px;" />
          <span class="mf-skeleton-block" style="width: 75px; height: 20px; border-radius: 4px;" />
        </div>

        <div class="sparkline-placeholder">
          <span class="mf-skeleton-block" style="width: 100%; height: 38px; border-radius: 6px;" />
        </div>

        <div class="card-footer-stats">
          <span class="mf-skeleton-block" style="width: 70px; height: 12px;" />
          <span class="mf-skeleton-block" style="width: 70px; height: 12px;" />
        </div>
      </div>
    </div>
  </div>
</template>

<script setup lang="ts">
// FinTech Glassmorphic Skeleton for Global Indices
</script>

<style scoped>
.market-skeleton-container {
  display: flex;
  flex-direction: column;
}

.skeleton-ribbon {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 10px 14px;
  margin-bottom: 12px;
  gap: 12px;
  flex-wrap: wrap;
}

.ribbon-left {
  display: flex;
  align-items: center;
  gap: 12px;
  flex-wrap: wrap;
}

.pills-row {
  display: flex;
  align-items: center;
  gap: 8px;
}

.skeleton-pill {
  width: 95px;
  height: 22px;
  border-radius: 12px;
}

.ribbon-right {
  display: flex;
  align-items: center;
  gap: 8px;
}

.skeleton-filter-bar {
  display: flex;
  align-items: center;
  justify-content: space-between;
  margin-bottom: 14px;
  gap: 12px;
  flex-wrap: wrap;
}

.category-pills {
  display: flex;
  align-items: center;
  gap: 8px;
}

.skeleton-filter-pill {
  width: 72px;
  height: 32px;
  border-radius: var(--mf-radius-md);
}

.indices-grid {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(280px, 1fr));
  gap: 14px;
}

.index-skeleton-card {
  padding: 14px;
}

.card-top {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 12px;
}

.name-box {
  display: flex;
  align-items: center;
  gap: 6px;
}

.flag-placeholder {
  width: 18px;
  height: 18px;
  border-radius: 50%;
}

.card-price-row {
  display: flex;
  justify-content: space-between;
  align-items: baseline;
  margin-bottom: 12px;
}

.sparkline-placeholder {
  margin: 6px 0 10px 0;
}

.card-footer-stats {
  display: flex;
  justify-content: space-between;
}
</style>
