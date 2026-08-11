<template>
  <el-tree
    :data="categories"
    :props="{ label: 'name', children: 'children' }"
    node-key="id"
    default-expand-all
  />
</template>

<script setup lang="ts">
import type { Category } from '@/types'

defineProps<{ categories: Category[] }>()
</script>
