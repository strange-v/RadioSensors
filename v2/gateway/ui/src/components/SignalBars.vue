<script setup lang="ts">
// Link quality as bar count rather than a raw dBm figure: "-91 dBm" means
// nothing without the scale, "two bars of four" reads instantly. Thresholds
// are for sub-GHz RFM69 links, which run far below Wi-Fi numbers.
import { computed } from 'vue'

const props = defineProps<{ rssi?: number }>()

const level = computed(() => {
  if (props.rssi === undefined) return 0
  if (props.rssi >= -65) return 4
  if (props.rssi >= -78) return 3
  if (props.rssi >= -88) return 2
  if (props.rssi >= -100) return 1
  return 0
})
</script>

<template>
  <span class="signal-bars" :class="{ weak: level <= 1 }" :aria-label="rssi === undefined ? undefined : `${rssi} dBm`">
    <i v-for="bar in 4" :key="bar" :class="{ on: bar <= level }"></i>
  </span>
</template>

<style scoped>
.signal-bars { display: inline-flex; align-items: flex-end; gap: 2px; height: 13px; color: var(--primary); }
.signal-bars.weak { color: var(--warning); }
.signal-bars i { width: 3px; border-radius: 1px; background: var(--line-strong); }
.signal-bars i.on { background: currentColor; }
.signal-bars i:nth-child(1) { height: 4px; }
.signal-bars i:nth-child(2) { height: 7px; }
.signal-bars i:nth-child(3) { height: 10px; }
.signal-bars i:nth-child(4) { height: 13px; }
</style>
