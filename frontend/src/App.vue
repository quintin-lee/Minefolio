<template>
  <router-view />
</template>

<script setup lang="ts">
import { onMounted } from 'vue'
import { useAuthStore } from '@/stores/auth'
import { useThemeStore } from '@/stores/theme'

const auth = useAuthStore()
const theme = useThemeStore()

onMounted(() => {
  theme.applyTheme()
  if (auth.token) {
    auth.fetchUser()
  }
})
</script>

<style scoped>
.mf-route-enter-active,
.mf-route-leave-active {
  transition: opacity 0.25s ease, transform 0.25s ease;
}
.mf-route-enter-from {
  opacity: 0;
  transform: translateY(8px);
}
.mf-route-leave-to {
  opacity: 0;
  transform: translateY(-4px);
}
</style>
