#pragma once

// Copy this file to LocalSecrets.h and choose a strong installation-specific
// password. LocalSecrets.h is excluded from Git.
#define GATEWAY_OTA_PASSWORD "replace-with-a-strong-password"

// Exactly 16 ASCII bytes shared by the gateway and all nodes in one
// installation. Leave empty until the installation key is chosen.
#define GATEWAY_RFM69_ENCRYPTION_KEY ""

// Exactly 16 ASCII bytes shared by unprovisioned nodes. This is distinct from
// the installation key above. An empty value disables commissioning mode.
#define GATEWAY_RFM69_COMMISSIONING_KEY ""
