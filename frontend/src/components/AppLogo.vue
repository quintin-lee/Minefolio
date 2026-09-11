<template>
  <div class="app-logo" :class="{ 'is-animated': animated }">
    <div class="app-logo-icon" :style="{ width: `${parsedSize}px`, height: `${parsedSize}px` }">
      <svg
        xmlns="http://www.w3.org/2000/svg"
        viewBox="0 0 512 512"
        class="logo-svg"
        fill="none"
      >
        <defs>
          <!-- Background shield gradient -->
          <radialGradient id="mfLogoBg" cx="50%" cy="25%" r="80%">
            <stop offset="0%" stop-color="#0e1b33" />
            <stop offset="100%" stop-color="#060b18" />
          </radialGradient>

          <!-- Left pillar gradient: Solid asset base (Indigo to Violet) -->
          <linearGradient id="mfLeftPillar" x1="0%" y1="100%" x2="0%" y2="0%">
            <stop offset="0%" stop-color="#4338ca" />
            <stop offset="50%" stop-color="#6366f1" />
            <stop offset="100%" stop-color="#818cf8" />
          </linearGradient>

          <!-- Left bevel / inner depth -->
          <linearGradient id="mfLeftInner" x1="0%" y1="0%" x2="100%" y2="100%">
            <stop offset="0%" stop-color="#312e81" />
            <stop offset="100%" stop-color="#1e1b4b" />
          </linearGradient>

          <!-- Right momentum trendline: Aurora Cyan to Sky Blue -->
          <linearGradient id="mfRightSurge" x1="0%" y1="100%" x2="100%" y2="0%">
            <stop offset="0%" stop-color="#0284c7" />
            <stop offset="45%" stop-color="#00d4ff" />
            <stop offset="100%" stop-color="#38bdf8" />
          </linearGradient>

          <!-- Right inner bevel -->
          <linearGradient id="mfRightInner" x1="100%" y1="100%" x2="0%" y2="0%">
            <stop offset="0%" stop-color="#0369a1" />
            <stop offset="100%" stop-color="#082f49" />
          </linearGradient>

          <!-- Peak glow filter -->
          <filter id="mfGlow" x="-20%" y="-20%" width="140%" height="140%">
            <feGaussianBlur stdDeviation="8" result="blur" />
            <feComposite in="SourceGraphic" in2="blur" operator="over" />
          </filter>
        </defs>

        <!-- Rounded shield backing -->
        <rect x="24" y="24" width="464" height="464" rx="112" fill="url(#mfLogoBg)" />
        <rect
          x="24"
          y="24"
          width="464"
          height="464"
          rx="112"
          fill="none"
          stroke="rgba(0, 212, 255, 0.25)"
          stroke-width="6"
        />

        <!-- Ambient glow inside shield -->
        <circle cx="360" cy="180" r="90" fill="#00d4ff" opacity="0.12" filter="url(#mfGlow)" />
        <circle cx="160" cy="340" r="100" fill="#6366f1" opacity="0.15" filter="url(#mfGlow)" />

        <!-- 3D Geometric Faceted "M" -->
        <!-- Left Pillar Face -->
        <path
          d="M100 376 V176 L164 136 V376 H100 Z"
          fill="url(#mfLeftPillar)"
        />

        <!-- Left Inward Slope (Depth shadow) -->
        <path
          d="M164 136 L248 296 L220 336 L164 230 V376 H164 Z"
          fill="url(#mfLeftInner)"
          opacity="0.9"
        />

        <!-- Center V-Peak reflection -->
        <path
          d="M164 136 L248 296 L276 244 L196 116 Z"
          fill="#818cf8"
          opacity="0.8"
        />

        <!-- Right Rising Momentum Diagonal (Center valley to peak) -->
        <path
          d="M248 296 L364 120 L404 146 L276 348 Z"
          fill="url(#mfRightSurge)"
        />

        <!-- Right Inward Slope (Depth) -->
        <path
          d="M276 348 L404 146 L380 376 H330 L310 270 Z"
          fill="url(#mfRightInner)"
        />

        <!-- Right Pillar Wing -->
        <path
          d="M344 376 V224 L408 140 V376 H344 Z"
          fill="url(#mfRightSurge)"
        />

        <!-- Upward Momentum Trend Arrow at top-right -->
        <g filter="url(#mfGlow)">
          <path
            d="M380 108 L436 104 L432 160 L408 136 L366 182 L344 160 L386 114 Z"
            fill="#38bdf8"
          />
        </g>
      </svg>
    </div>

    <span v-if="withText" class="logo-title">Minefolio</span>
  </div>
</template>

<script setup lang="ts">
import { computed } from 'vue'

const props = withDefaults(
  defineProps<{
    size?: number | string
    withText?: boolean
    animated?: boolean
  }>(),
  {
    size: 32,
    withText: false,
    animated: true,
  },
)

const parsedSize = computed(() => {
  const s = typeof props.size === 'string' ? parseInt(props.size, 10) : props.size
  return Number.isNaN(s) || s <= 0 ? 32 : s
})
</script>

<style scoped>
.app-logo {
  display: inline-flex;
  align-items: center;
  gap: 10px;
  user-select: none;
  line-height: 1;
}

.app-logo-icon {
  display: inline-flex;
  align-items: center;
  justify-content: center;
  flex-shrink: 0;
  transition: transform 0.25s cubic-bezier(0.34, 1.56, 0.64, 1), filter 0.25s ease;
}

.logo-svg {
  width: 100%;
  height: 100%;
  display: block;
}

.logo-title {
  font-size: 19px;
  font-weight: 700;
  letter-spacing: 0.8px;
  background: linear-gradient(135deg, var(--mf-primary, #00d4ff) 0%, #a78bfa 100%);
  -webkit-background-clip: text;
  -webkit-text-fill-color: transparent;
  font-family: inherit;
}

.app-logo.is-animated:hover .app-logo-icon {
  transform: translateY(-2px) scale(1.06);
  filter: drop-shadow(0 4px 12px rgba(0, 212, 255, 0.35));
}
</style>
