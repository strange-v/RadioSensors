#pragma once

#include <stddef.h>
#include <stdint.h>

namespace gateway::release {

constexpr size_t kMaxManifestSize = 4096;
// DER-encoded ECDSA P-256 signature: at most 72 bytes.
constexpr size_t kMaxSignatureSize = 72;
// Base64 of the largest signature.
constexpr size_t kMaxSignatureLineLength = 96;
// manifest.signed: the signature line, "\n", then the exact manifest bytes.
constexpr size_t kMaxSignedManifestSize =
    kMaxSignatureLineLength + 1 + kMaxManifestSize;
constexpr size_t kMaxFileNameLength = 64;
constexpr size_t kMaxVersionLength = 17;  // 65535.65535.65535
constexpr size_t kSha256Size = 32;

// The key CI signs manifest.json with; gateway-signing.pub.pem holds the same
// key and the native test keeps the two equal.
inline constexpr char kSigningPublicKey[] =
    "-----BEGIN PUBLIC KEY-----\n"
    "MFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAEkGkJtPrG6vUgRL5tJd0RjforO3q4\n"
    "ymAvB8iSisq4fxNYBjJ9dplfW1/4CmVE2et/QmMDeA91B1Sen5SiYVxLdg==\n"
    "-----END PUBLIC KEY-----\n";

struct Version {
    uint16_t major = 0;
    uint16_t minor = 0;
    uint16_t patch = 0;
};

struct Image {
    char file[kMaxFileNameLength + 1]{};
    uint32_t size = 0;
    uint8_t sha256[kSha256Size]{};
};

struct Release {
    Version version;
    char versionText[kMaxVersionLength + 1]{};
    Image firmware;
    Image ui;
};

enum class Status : uint8_t {
    Ok,
    TooLarge,
    BadSignature,
    Malformed,
    UnsupportedFormat,
    BoardMissing,
    NotNewer,
};

// Strict major.minor.patch: decimal, no leading zeros, each part <= 65535.
bool parseVersion(const char* text, Version& version);
int compare(const Version& left, const Version& right);

bool verifySignature(
    const uint8_t* manifest, size_t size,
    const uint8_t* signature, size_t signatureSize);

// Reads an already verified manifest: the entry for `board`, if it is newer
// than `currentVersion`.
Status parse(
    const uint8_t* manifest, size_t size, const char* board,
    const char* currentVersion, Release& release);

// Signature first, then parse(): nothing unsigned is ever interpreted.
Status read(
    const uint8_t* manifest, size_t size,
    const uint8_t* signature, size_t signatureSize,
    const char* board, const char* currentVersion, Release& release);

// Splits manifest.signed and calls read().
Status readSigned(
    const uint8_t* signedManifest, size_t size,
    const char* board, const char* currentVersion, Release& release);

const char* statusName(Status status);

}  // namespace gateway::release
