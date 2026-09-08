// The administration bundle: every view only an admin can open.
//
// Both routes import this one module rather than each view directly, so Rollup
// emits a single chunk. Given two separate dynamic imports it produced three
// files -- one per view, plus a third holding the utils/tokens.ts they share --
// and on an ESP32 each extra file costs a round trip and a whole 4 KB LittleFS
// block, which that 280-byte shared stub does not come close to earning.
//
// Splitting them again would only make sense if one grew large enough that
// loading it with the other became the greater waste.
export { default as AdminView } from './AdminView.vue'
export { default as HomeAssistantView } from './HomeAssistantView.vue'
