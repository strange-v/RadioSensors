export interface SetupStatus { setup_required: boolean; physical_window_active: boolean; remaining_seconds: number }
export interface SetupRequest { username: string; password: string; display_name?: string; operational_network_id?: number }
export interface SessionUser { id: number; username: string; role: 'admin' | 'viewer' }
export interface Session { user: SessionUser; csrf_token: string }
export type TokenScope = 'gateway:read' | 'registry:read' | 'telemetry:read'
export interface ApiToken { id: number; name: string; enabled: boolean; created_at_ms: number; scopes: TokenScope[] }
export interface CreatedApiToken extends ApiToken { token: string }
export interface ApiTokenList { tokens: ApiToken[] }
export interface GatewaySettings { generation: number; display_name: string; mdns_enabled: boolean; ntp_enabled: boolean; pairing_window_seconds: number; setup_window_seconds: number; ntp_servers: string[] }
export interface PairingStatus { active: boolean; remaining_seconds: number; indication: string }
export interface GatewayInfo { firmware_version: string; api_version: number; display_name: string; ui: { state: string; version: string; required_api_version: number }; board: string; hostname: string }
export interface GatewayNode {
  node_id: number
  // Immutable uppercase factory UID. With gateway_id this is the stable Home
  // Assistant identity; the radio node_id is reusable and is not.
  device_uid: string
  display_name: string
  profile_id: number
  firmware: string
  state: 'pending' | 'active' | 'disabled' | string
  last_seen_at_ms?: number
  rssi?: number
  has_telemetry?: boolean
}
export interface RadioNetworkReset { operational_network_id: number; removed_nodes: number; restarting: boolean }
export interface RenamedNode { node_id: number; display_name: string; registry_generation: number }
export interface NodeRegistry { registry_generation: number; nodes: GatewayNode[] }
export interface Health {
  status: string; firmware: string; api_version: number; board: string; hostname: string; gateway_id: string; boot_id: string; reset_reason: string; uptime_ms: number; free_heap: number;
  registry: { records: number; generation: number }; setup: { required: boolean; active: boolean; remaining_seconds: number };
  pairing: PairingStatus;
  storage: { ready: boolean; settings_generation: number; auth_generation: number; secrets_generation: number };
  ethernet: { state: string; has_ip: boolean; ip: string; mac: string }; ota: { enabled: boolean; state: string; progress: number };
  web_ui: { state: string; version: string; required_api_version: number }; telemetry: { nodes_seen: number; updates: number; last_node_id: number; last_received_at_ms: number };
  time: { state: string; unix_ms: number; last_sync_ms: number }; websocket: { clients: number; connections: number; messages_sent: number; messages_dropped: number };
  radio: { state: string; present: boolean; version: number; frequency_band_mhz: string; frequency_hz: number; bit_rate: number; node_id: number; network_id: number; variant: string; configured_power_dbm: number; encryption_enabled: boolean; profile: string; spi_host: string; pins: Record<string, number>; counters: Record<string, number>; last_packet: { at_ms: number; sender_id: number; rssi: number } };
  [key: string]: unknown;
}
