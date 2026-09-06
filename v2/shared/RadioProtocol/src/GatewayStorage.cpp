#include "GatewayStorage.h"

#include "JoinRequest.h"

namespace radiosensors::gateway_storage {
namespace {

constexpr uint8_t kSettingsMagic[4] = {'R', 'S', 'G', 'C'};
constexpr uint8_t kAuthMagic[4] = {'R', 'S', 'A', 'U'};
constexpr uint8_t kSecretsMagic[4] = {'R', 'S', 'G', 'S'};
constexpr uint16_t kKnownTokenScopes = GatewayRead | RegistryRead | TelemetryRead;

uint32_t crc32(const uint8_t* data, const size_t size) {
    uint32_t crc = 0xFFFFFFFFUL;
    for (size_t index = 0; index < size; ++index) {
        crc ^= data[index];
        for (uint8_t bit = 0; bit < 8; ++bit) {
            const uint32_t mask = static_cast<uint32_t>(-static_cast<int32_t>(crc & 1U));
            crc = (crc >> 1U) ^ (0xEDB88320UL & mask);
        }
    }
    return ~crc;
}

void write64(uint8_t* output, const uint64_t value) {
    protocol::writeUint32Le(output, static_cast<uint32_t>(value));
    protocol::writeUint32Le(output + 4, static_cast<uint32_t>(value >> 32U));
}

uint64_t read64(const uint8_t* input) {
    return static_cast<uint64_t>(protocol::readUint32Le(input)) |
        (static_cast<uint64_t>(protocol::readUint32Le(input + 4)) << 32U);
}

bool allZero(const uint8_t* data, const size_t size) {
    for (size_t index = 0; index < size; ++index) if (data[index] != 0) return false;
    return true;
}

bool validUtf8(const char* data, const size_t size) {
    size_t index = 0;
    while (index < size) {
        const uint8_t first = static_cast<uint8_t>(data[index++]);
        if (first == 0 || first < 0x20 || first == 0x7F) return false;
        if (first < 0x80) continue;
        uint8_t continuation = 0;
        uint32_t codePoint = 0;
        if ((first & 0xE0) == 0xC0) { continuation = 1; codePoint = first & 0x1F; }
        else if ((first & 0xF0) == 0xE0) { continuation = 2; codePoint = first & 0x0F; }
        else if ((first & 0xF8) == 0xF0) { continuation = 3; codePoint = first & 0x07; }
        else return false;
        if (index + continuation > size) return false;
        for (uint8_t part = 0; part < continuation; ++part) {
            const uint8_t next = static_cast<uint8_t>(data[index++]);
            if ((next & 0xC0) != 0x80) return false;
            codePoint = (codePoint << 6U) | (next & 0x3F);
        }
        if ((continuation == 1 && codePoint < 0x80) ||
            (continuation == 2 && codePoint < 0x800) ||
            (continuation == 3 && codePoint < 0x10000) ||
            codePoint > 0x10FFFF || (codePoint >= 0xD800 && codePoint <= 0xDFFF)) return false;
    }
    return true;
}

bool validUsername(const char* data, const size_t size) {
    if (size == 0 || size > kUsernameSize) return false;
    for (size_t index = 0; index < size; ++index) {
        const char value = data[index];
        if (!((value >= 'a' && value <= 'z') || (value >= '0' && value <= '9') ||
              value == '.' || value == '_' || value == '-')) return false;
    }
    return true;
}

bool validNtpServer(const char* data, const size_t size) {
    if (size == 0 || size > kNtpServerSize) return false;
    for (size_t index = 0; index < size; ++index) {
        const uint8_t value = static_cast<uint8_t>(data[index]);
        if (value <= 0x20 || value >= 0x7F || value == '/' || value == '@') return false;
    }
    return true;
}

bool zeroPadded(const uint8_t* data, const size_t used, const size_t capacity) {
    return used <= capacity && allZero(data + used, capacity - used);
}

void writeHeader(uint8_t* output, const uint8_t* magic, const uint32_t generation,
                 const uint16_t size) {
    for (uint8_t index = 0; index < 4; ++index) output[index] = magic[index];
    protocol::writeUint16Le(output + 4, kStorageVersion);
    protocol::writeUint32Le(output + 6, generation);
    protocol::writeUint16Le(output + 10, size);
}

CodecStatus validateHeader(const uint8_t* data, const size_t size,
                           const uint8_t* magic, const size_t expectedSize,
                           uint32_t& generation) {
    if (data == nullptr || size != expectedSize ||
        protocol::readUint16Le(data + 10) != expectedSize) return CodecStatus::InvalidSize;
    for (uint8_t index = 0; index < 4; ++index)
        if (data[index] != magic[index]) return CodecStatus::InvalidMagic;
    if (protocol::readUint16Le(data + 4) != kStorageVersion)
        return CodecStatus::UnsupportedVersion;
    if (protocol::readUint32Le(data + size - 4) != crc32(data, size - 4))
        return CodecStatus::CrcMismatch;
    generation = protocol::readUint32Le(data + 6);
    return CodecStatus::Ok;
}

CodecStatus validateSettings(const GatewaySettings& value) {
    if (value.displayNameLength > kDisplayNameSize ||
        !zeroPadded(reinterpret_cast<const uint8_t*>(value.displayName),
                    value.displayNameLength, kDisplayNameSize) ||
        (value.displayNameLength != 0 &&
         !validUtf8(value.displayName, value.displayNameLength))) return CodecStatus::InvalidString;
    if (value.ntpServerCount > kNtpServerCount ||
        (value.ntpEnabled && value.ntpServerCount == 0)) return CodecStatus::InvalidCount;
    if (value.pairingWindowSeconds < 30 || value.pairingWindowSeconds > 900 ||
        value.setupWindowSeconds < 60 || value.setupWindowSeconds > 1800)
        return CodecStatus::InvalidValue;
    for (size_t index = 0; index < kNtpServerCount; ++index) {
        const size_t length = value.ntpServerLengths[index];
        if (index >= value.ntpServerCount) {
            if (length != 0 || !allZero(reinterpret_cast<const uint8_t*>(value.ntpServers[index]),
                                        kNtpServerSize)) return CodecStatus::InvalidReservedData;
            continue;
        }
        if (!validNtpServer(value.ntpServers[index], length) ||
            !zeroPadded(reinterpret_cast<const uint8_t*>(value.ntpServers[index]),
                        length, kNtpServerSize)) return CodecStatus::InvalidString;
        for (size_t previous = 0; previous < index; ++previous)
            if (length == value.ntpServerLengths[previous] &&
                memcmp(value.ntpServers[index], value.ntpServers[previous], length) == 0)
                return CodecStatus::DuplicateValue;
    }
    return CodecStatus::Ok;
}

CodecStatus validateAuthentication(const AuthenticationData& value) {
    if (value.userCount > kMaximumUsers || value.tokenCount > kMaximumTokens)
        return CodecStatus::InvalidCount;
    bool enabledAdmin = false;
    uint32_t maximumUserId = 0;
    uint32_t maximumTokenId = 0;
    for (size_t index = 0; index < kMaximumUsers; ++index) {
        const UserRecord& user = value.users[index];
        if (index >= value.userCount) {
            if (user.id != 0 || user.usernameLength != 0 ||
                !allZero(reinterpret_cast<const uint8_t*>(user.username), kUsernameSize) ||
                static_cast<uint8_t>(user.role) != 0 || user.enabled ||
                static_cast<uint8_t>(user.hashAlgorithm) != 0 || user.pbkdf2Iterations != 0 ||
                !allZero(user.salt, kPasswordSaltSize) ||
                !allZero(user.passwordHash, kPasswordHashSize))
                return CodecStatus::InvalidReservedData;
            continue;
        }
        if (user.id == 0 || !validUsername(user.username, user.usernameLength) ||
            !zeroPadded(reinterpret_cast<const uint8_t*>(user.username),
                        user.usernameLength, kUsernameSize)) return CodecStatus::InvalidValue;
        if (user.role != UserRole::Admin && user.role != UserRole::Viewer)
            return CodecStatus::InvalidValue;
        if (user.hashAlgorithm != PasswordHashAlgorithm::Pbkdf2HmacSha256 ||
            user.pbkdf2Iterations < kMinimumPbkdf2Iterations ||
            user.pbkdf2Iterations > kMaximumPbkdf2Iterations ||
            allZero(user.salt, sizeof(user.salt)) ||
            allZero(user.passwordHash, sizeof(user.passwordHash))) return CodecStatus::InvalidValue;
        if (user.enabled && user.role == UserRole::Admin) enabledAdmin = true;
        if (user.id > maximumUserId) maximumUserId = user.id;
        for (size_t previous = 0; previous < index; ++previous)
            if (user.id == value.users[previous].id ||
                (user.usernameLength == value.users[previous].usernameLength &&
                 memcmp(user.username, value.users[previous].username,
                        user.usernameLength) == 0)) return CodecStatus::DuplicateValue;
    }
    if (value.userCount != 0 && !enabledAdmin) return CodecStatus::MissingAdmin;
    for (size_t index = 0; index < kMaximumTokens; ++index) {
        const ApiTokenRecord& token = value.tokens[index];
        if (index >= value.tokenCount) {
            if (token.id != 0 || token.nameLength != 0 ||
                !allZero(reinterpret_cast<const uint8_t*>(token.name), kCredentialNameSize) ||
                token.enabled || token.scopes != 0 || token.createdAtUnixMs != 0 ||
                !allZero(token.tokenHash, kTokenHashSize))
                return CodecStatus::InvalidReservedData;
            continue;
        }
        if (token.id == 0 || token.nameLength == 0 || token.nameLength > kCredentialNameSize ||
            !validUtf8(token.name, token.nameLength) ||
            !zeroPadded(reinterpret_cast<const uint8_t*>(token.name), token.nameLength,
                        kCredentialNameSize) || token.scopes == 0 ||
            (token.scopes & ~kKnownTokenScopes) != 0 ||
            allZero(token.tokenHash, sizeof(token.tokenHash))) return CodecStatus::InvalidValue;
        if (token.id > maximumTokenId) maximumTokenId = token.id;
        for (size_t previous = 0; previous < index; ++previous)
            if (token.id == value.tokens[previous].id) return CodecStatus::DuplicateValue;
    }
    if ((value.userCount != 0 &&
         (maximumUserId == UINT32_MAX || value.nextUserId <= maximumUserId)) ||
        (value.tokenCount != 0 &&
         (maximumTokenId == UINT32_MAX || value.nextTokenId <= maximumTokenId)))
        return CodecStatus::InvalidValue;
    return CodecStatus::Ok;
}

CodecStatus validateSecrets(const InstallationSecrets& value) {
    if (!value.installationKeyPresent &&
        (value.operationalNetworkId != 0 || !allZero(value.installationKey, kRadioKeySize)))
        return CodecStatus::InvalidValue;
    if (value.installationKeyPresent && value.operationalNetworkId == 0)
        return CodecStatus::InvalidValue;
    if (!value.commissioningKeyPresent &&
        (value.commissioningNetworkId != 0 || !allZero(value.commissioningKey, kRadioKeySize)))
        return CodecStatus::InvalidValue;
    if (!value.deviceSecretPresent && !allZero(value.deviceSecret, kDeviceSecretSize))
        return CodecStatus::InvalidValue;
    if (value.installationKeyPresent && value.commissioningKeyPresent &&
        value.operationalNetworkId == value.commissioningNetworkId)
        return CodecStatus::InvalidValue;
    return CodecStatus::Ok;
}

bool bytesEqual(const void* left, const void* right, const size_t size) {
    return memcmp(left, right, size) == 0;
}

}  // namespace

GatewaySettings defaultSettings() {
    GatewaySettings value{};
    value.mdnsEnabled = true;
    value.ntpEnabled = true;
    value.pairingWindowSeconds = 120;
    value.setupWindowSeconds = 600;
    const char* servers[] = {"pool.ntp.org", "time.cloudflare.com"};
    value.ntpServerCount = 2;
    for (size_t index = 0; index < 2; ++index) {
        value.ntpServerLengths[index] = static_cast<uint8_t>(strlen(servers[index]));
        memcpy(value.ntpServers[index], servers[index], value.ntpServerLengths[index]);
    }
    return value;
}

AuthenticationData defaultAuthentication() {
    AuthenticationData value{};
    value.nextUserId = 1;
    value.nextTokenId = 1;
    return value;
}

InstallationSecrets defaultSecrets() { return InstallationSecrets{}; }

bool settingsEqual(const GatewaySettings& left, const GatewaySettings& right) {
    return left.mdnsEnabled == right.mdnsEnabled && left.ntpEnabled == right.ntpEnabled &&
        left.pairingWindowSeconds == right.pairingWindowSeconds &&
        left.setupWindowSeconds == right.setupWindowSeconds &&
        left.displayNameLength == right.displayNameLength &&
        bytesEqual(left.displayName, right.displayName, kDisplayNameSize) &&
        left.ntpServerCount == right.ntpServerCount &&
        bytesEqual(left.ntpServerLengths, right.ntpServerLengths, kNtpServerCount) &&
        bytesEqual(left.ntpServers, right.ntpServers, sizeof(left.ntpServers));
}
bool authenticationEqual(const AuthenticationData& left, const AuthenticationData& right) {
    if (left.userCount != right.userCount || left.tokenCount != right.tokenCount ||
        left.nextUserId != right.nextUserId || left.nextTokenId != right.nextTokenId)
        return false;
    for (size_t index = 0; index < kMaximumUsers; ++index) {
        const UserRecord& a = left.users[index];
        const UserRecord& b = right.users[index];
        if (a.id != b.id || a.usernameLength != b.usernameLength ||
            !bytesEqual(a.username, b.username, kUsernameSize) || a.role != b.role ||
            a.enabled != b.enabled || a.hashAlgorithm != b.hashAlgorithm ||
            a.pbkdf2Iterations != b.pbkdf2Iterations ||
            !bytesEqual(a.salt, b.salt, kPasswordSaltSize) ||
            !bytesEqual(a.passwordHash, b.passwordHash, kPasswordHashSize)) return false;
    }
    for (size_t index = 0; index < kMaximumTokens; ++index) {
        const ApiTokenRecord& a = left.tokens[index];
        const ApiTokenRecord& b = right.tokens[index];
        if (a.id != b.id || a.nameLength != b.nameLength ||
            !bytesEqual(a.name, b.name, kCredentialNameSize) || a.enabled != b.enabled ||
            a.scopes != b.scopes || a.createdAtUnixMs != b.createdAtUnixMs ||
            !bytesEqual(a.tokenHash, b.tokenHash, kTokenHashSize)) return false;
    }
    return true;
}
bool secretsEqual(const InstallationSecrets& left, const InstallationSecrets& right) {
    return left.installationKeyPresent == right.installationKeyPresent &&
        left.commissioningKeyPresent == right.commissioningKeyPresent &&
        left.deviceSecretPresent == right.deviceSecretPresent &&
        left.operationalNetworkId == right.operationalNetworkId &&
        left.commissioningNetworkId == right.commissioningNetworkId &&
        bytesEqual(left.installationKey, right.installationKey, kRadioKeySize) &&
        bytesEqual(left.commissioningKey, right.commissioningKey, kRadioKeySize) &&
        bytesEqual(left.deviceSecret, right.deviceSecret, kDeviceSecretSize);
}

CodecStatus encodeSettings(const GatewaySettings& value, const uint32_t generation,
                           uint8_t* output, const size_t capacity) {
    if (output == nullptr || capacity < kSettingsSnapshotSize) return CodecStatus::OutputTooSmall;
    const CodecStatus status = validateSettings(value);
    if (status != CodecStatus::Ok) return status;
    memset(output, 0, kSettingsSnapshotSize);
    writeHeader(output, kSettingsMagic, generation, kSettingsSnapshotSize);
    output[12] = (value.mdnsEnabled ? 1U : 0U) | (value.ntpEnabled ? 2U : 0U);
    output[13] = value.displayNameLength;
    output[14] = value.ntpServerCount;
    protocol::writeUint16Le(output + 16, value.pairingWindowSeconds);
    protocol::writeUint16Le(output + 18, value.setupWindowSeconds);
    memcpy(output + 20, value.displayName, kDisplayNameSize);
    for (size_t index = 0; index < kNtpServerCount; ++index) {
        const size_t offset = 68 + index * 64;
        output[offset] = value.ntpServerLengths[index];
        memcpy(output + offset + 1, value.ntpServers[index], kNtpServerSize);
    }
    protocol::writeUint32Le(output + 260, crc32(output, 260));
    return CodecStatus::Ok;
}

CodecStatus decodeSettings(const uint8_t* data, const size_t size,
                           GatewaySettings& value, uint32_t& generation) {
    CodecStatus status = validateHeader(data, size, kSettingsMagic,
                                        kSettingsSnapshotSize, generation);
    if (status != CodecStatus::Ok) return status;
    if ((data[12] & ~3U) != 0) return CodecStatus::InvalidFlags;
    if (data[15] != 0) return CodecStatus::InvalidReservedData;
    GatewaySettings candidate{};
    candidate.mdnsEnabled = (data[12] & 1U) != 0;
    candidate.ntpEnabled = (data[12] & 2U) != 0;
    candidate.displayNameLength = data[13];
    candidate.ntpServerCount = data[14];
    candidate.pairingWindowSeconds = protocol::readUint16Le(data + 16);
    candidate.setupWindowSeconds = protocol::readUint16Le(data + 18);
    memcpy(candidate.displayName, data + 20, kDisplayNameSize);
    for (size_t index = 0; index < kNtpServerCount; ++index) {
        const size_t offset = 68 + index * 64;
        candidate.ntpServerLengths[index] = data[offset];
        memcpy(candidate.ntpServers[index], data + offset + 1, kNtpServerSize);
    }
    status = validateSettings(candidate);
    if (status == CodecStatus::Ok) value = candidate;
    return status;
}

CodecStatus encodeAuthentication(const AuthenticationData& value,
        const uint32_t generation, uint8_t* output, const size_t capacity) {
    if (output == nullptr || capacity < kAuthSnapshotSize) return CodecStatus::OutputTooSmall;
    const CodecStatus status = validateAuthentication(value);
    if (status != CodecStatus::Ok) return status;
    memset(output, 0, kAuthSnapshotSize);
    writeHeader(output, kAuthMagic, generation, kAuthSnapshotSize);
    output[12] = value.userCount;
    output[13] = value.tokenCount;
    protocol::writeUint32Le(output + 16, value.nextUserId);
    protocol::writeUint32Le(output + 20, value.nextTokenId);
    for (size_t index = 0; index < value.userCount; ++index) {
        const UserRecord& user = value.users[index];
        uint8_t* record = output + 24 + index * kStoredUserSize;
        protocol::writeUint32Le(record, user.id);
        record[4] = user.usernameLength;
        memcpy(record + 5, user.username, kUsernameSize);
        record[37] = static_cast<uint8_t>(user.role);
        record[38] = user.enabled ? 1 : 0;
        record[39] = static_cast<uint8_t>(user.hashAlgorithm);
        protocol::writeUint32Le(record + 40, user.pbkdf2Iterations);
        memcpy(record + 44, user.salt, kPasswordSaltSize);
        memcpy(record + 60, user.passwordHash, kPasswordHashSize);
    }
    for (size_t index = 0; index < value.tokenCount; ++index) {
        const ApiTokenRecord& token = value.tokens[index];
        uint8_t* record = output + 392 + index * kStoredTokenSize;
        protocol::writeUint32Le(record, token.id);
        record[4] = token.nameLength;
        memcpy(record + 5, token.name, kCredentialNameSize);
        record[37] = token.enabled ? 1 : 0;
        protocol::writeUint16Le(record + 38, token.scopes);
        write64(record + 40, token.createdAtUnixMs);
        memcpy(record + 48, token.tokenHash, kTokenHashSize);
    }
    protocol::writeUint32Le(output + 1032, crc32(output, 1032));
    return CodecStatus::Ok;
}

CodecStatus decodeAuthentication(const uint8_t* data, const size_t size,
        AuthenticationData& value, uint32_t& generation) {
    CodecStatus status = validateHeader(data, size, kAuthMagic,
                                        kAuthSnapshotSize, generation);
    if (status != CodecStatus::Ok) return status;
    if (data[14] != 0 || data[15] != 0) return CodecStatus::InvalidReservedData;
    AuthenticationData candidate{};
    candidate.userCount = data[12];
    candidate.tokenCount = data[13];
    if (candidate.userCount > kMaximumUsers || candidate.tokenCount > kMaximumTokens)
        return CodecStatus::InvalidCount;
    candidate.nextUserId = protocol::readUint32Le(data + 16);
    candidate.nextTokenId = protocol::readUint32Le(data + 20);
    for (size_t index = 0; index < candidate.userCount; ++index) {
        const uint8_t* record = data + 24 + index * kStoredUserSize;
        UserRecord& user = candidate.users[index];
        user.id = protocol::readUint32Le(record);
        user.usernameLength = record[4];
        memcpy(user.username, record + 5, kUsernameSize);
        user.role = static_cast<UserRole>(record[37]);
        if ((record[38] & ~1U) != 0) return CodecStatus::InvalidFlags;
        user.enabled = (record[38] & 1U) != 0;
        user.hashAlgorithm = static_cast<PasswordHashAlgorithm>(record[39]);
        user.pbkdf2Iterations = protocol::readUint32Le(record + 40);
        memcpy(user.salt, record + 44, kPasswordSaltSize);
        memcpy(user.passwordHash, record + 60, kPasswordHashSize);
    }
    for (size_t index = candidate.userCount; index < kMaximumUsers; ++index)
        if (!allZero(data + 24 + index * kStoredUserSize, kStoredUserSize))
            return CodecStatus::InvalidReservedData;
    for (size_t index = 0; index < candidate.tokenCount; ++index) {
        const uint8_t* record = data + 392 + index * kStoredTokenSize;
        ApiTokenRecord& token = candidate.tokens[index];
        token.id = protocol::readUint32Le(record);
        token.nameLength = record[4];
        memcpy(token.name, record + 5, kCredentialNameSize);
        if ((record[37] & ~1U) != 0) return CodecStatus::InvalidFlags;
        token.enabled = (record[37] & 1U) != 0;
        token.scopes = protocol::readUint16Le(record + 38);
        token.createdAtUnixMs = read64(record + 40);
        memcpy(token.tokenHash, record + 48, kTokenHashSize);
    }
    for (size_t index = candidate.tokenCount; index < kMaximumTokens; ++index)
        if (!allZero(data + 392 + index * kStoredTokenSize, kStoredTokenSize))
            return CodecStatus::InvalidReservedData;
    status = validateAuthentication(candidate);
    if (status == CodecStatus::Ok) value = candidate;
    return status;
}

CodecStatus encodeSecrets(const InstallationSecrets& value, const uint32_t generation,
                          uint8_t* output, const size_t capacity) {
    if (output == nullptr || capacity < kSecretsSnapshotSize) return CodecStatus::OutputTooSmall;
    const CodecStatus status = validateSecrets(value);
    if (status != CodecStatus::Ok) return status;
    memset(output, 0, kSecretsSnapshotSize);
    writeHeader(output, kSecretsMagic, generation, kSecretsSnapshotSize);
    uint16_t flags = 0;
    if (value.installationKeyPresent) flags |= 1U;
    if (value.commissioningKeyPresent) flags |= 2U;
    if (value.deviceSecretPresent) flags |= 4U;
    protocol::writeUint16Le(output + 12, flags);
    output[14] = value.operationalNetworkId;
    output[15] = value.commissioningNetworkId;
    memcpy(output + 16, value.installationKey, kRadioKeySize);
    memcpy(output + 32, value.commissioningKey, kRadioKeySize);
    memcpy(output + 48, value.deviceSecret, kDeviceSecretSize);
    protocol::writeUint32Le(output + 80, crc32(output, 80));
    return CodecStatus::Ok;
}

CodecStatus decodeSecrets(const uint8_t* data, const size_t size,
                          InstallationSecrets& value, uint32_t& generation) {
    CodecStatus status = validateHeader(data, size, kSecretsMagic,
                                        kSecretsSnapshotSize, generation);
    if (status != CodecStatus::Ok) return status;
    const uint16_t flags = protocol::readUint16Le(data + 12);
    if ((flags & ~7U) != 0) return CodecStatus::InvalidFlags;
    InstallationSecrets candidate{};
    candidate.installationKeyPresent = (flags & 1U) != 0;
    candidate.commissioningKeyPresent = (flags & 2U) != 0;
    candidate.deviceSecretPresent = (flags & 4U) != 0;
    candidate.operationalNetworkId = data[14];
    candidate.commissioningNetworkId = data[15];
    memcpy(candidate.installationKey, data + 16, kRadioKeySize);
    memcpy(candidate.commissioningKey, data + 32, kRadioKeySize);
    memcpy(candidate.deviceSecret, data + 48, kDeviceSecretSize);
    status = validateSecrets(candidate);
    if (status == CodecStatus::Ok) value = candidate;
    return status;
}

}  // namespace radiosensors::gateway_storage
