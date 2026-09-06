<template>
  <el-config-provider :locale="epLocale">
    <router-view />
  </el-config-provider>
</template>

<script setup lang="ts">
import { computed, onMounted, watch } from 'vue'
import { useI18n } from 'vue-i18n'
import zhCn from 'element-plus/es/locale/lang/zh-cn'
import en from 'element-plus/es/locale/lang/en'
import { useAuthStore } from '@/stores/auth'
import { useThemeStore } from '@/stores/theme'

const auth = useAuthStore()
const theme = useThemeStore()
const { locale } = useI18n()

const epLocale = computed(() => (locale.value === 'en-US' ? en : zhCn))

watch(
  locale,
  (l) => {
    document.documentElement.lang = l
  },
  { immediate: true },
)

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
