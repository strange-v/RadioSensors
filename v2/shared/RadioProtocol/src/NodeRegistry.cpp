#include "NodeRegistry.h"

#include <string.h>

namespace radiosensors {
namespace registry {

bool validDisplayName(const char* const value, const size_t length) {
    if ((value == nullptr && length != 0) || length > kNodeDisplayNameSize)
        return false;
    size_t index = 0;
    while (index < length) {
        const uint8_t first = static_cast<uint8_t>(value[index]);
        uint32_t codePoint = 0;
        size_t continuationCount = 0;
        if (first < 0x80) {
            codePoint = first;
        } else if (first >= 0xC2 && first <= 0xDF) {
            codePoint = first & 0x1F;
            continuationCount = 1;
        } else if (first >= 0xE0 && first <= 0xEF) {
            codePoint = first & 0x0F;
            continuationCount = 2;
        } else if (first >= 0xF0 && first <= 0xF4) {
            codePoint = first & 0x07;
            continuationCount = 3;
        } else {
            return false;
        }
        if (index + continuationCount >= length) return false;
        for (size_t offset = 1; offset <= continuationCount; ++offset) {
            const uint8_t next = static_cast<uint8_t>(value[index + offset]);
            if ((next & 0xC0) != 0x80) return false;
            codePoint = (codePoint << 6) | (next & 0x3F);
        }
        if ((continuationCount == 2 && codePoint < 0x800) ||
            (continuationCount == 3 && codePoint < 0x10000) ||
            codePoint > 0x10FFFF ||
            (codePoint >= 0xD800 && codePoint <= 0xDFFF) ||
            codePoint < 0x20 || (codePoint >= 0x7F && codePoint <= 0x9F)) {
            return false;
        }
        index += continuationCount + 1;
    }
    return true;
}

NodeRegistry::NodeRegistry() : records_{}, count_(0) {}

size_t NodeRegistry::size() const {
    return count_;
}

const NodeRecord* NodeRegistry::records() const {
    return records_;
}

bool NodeRegistry::uidEquals(const uint8_t* left, const uint8_t* right) {
    if (left == nullptr || right == nullptr) {
        return false;
    }
    for (size_t index = 0; index < protocol::kDeviceUidSize; ++index) {
        if (left[index] != right[index]) {
            return false;
        }
    }
    return true;
}

bool NodeRegistry::firmwareEquals(
    const protocol::FirmwareVersion& left,
    const protocol::FirmwareVersion& right) {
    return left.major == right.major &&
           left.minor == right.minor &&
           left.patch == right.patch;
}

const NodeRecord* NodeRegistry::findByUid(const uint8_t* deviceUid) const {
    for (size_t index = 0; index < count_; ++index) {
        if (uidEquals(records_[index].deviceUid, deviceUid)) {
            return &records_[index];
        }
    }
    return nullptr;
}

const NodeRecord* NodeRegistry::findByNodeId(const uint8_t nodeId) const {
    for (size_t index = 0; index < count_; ++index) {
        if (records_[index].nodeId == nodeId) {
            return &records_[index];
        }
    }
    return nullptr;
}

NodeRecord* NodeRegistry::findMutableByUid(const uint8_t* deviceUid) {
    return const_cast<NodeRecord*>(
        static_cast<const NodeRegistry*>(this)->findByUid(deviceUid));
}

NodeRecord* NodeRegistry::findMutableByNodeId(const uint8_t nodeId) {
    return const_cast<NodeRecord*>(
        static_cast<const NodeRegistry*>(this)->findByNodeId(nodeId));
}

uint8_t NodeRegistry::allocateNodeId() const {
    for (uint16_t candidate = kFirstNodeId; candidate <= kLastNodeId; ++candidate) {
        if (findByNodeId(static_cast<uint8_t>(candidate)) == nullptr) {
            return static_cast<uint8_t>(candidate);
        }
    }
    return kUnprovisionedNodeId;
}

ReserveResult NodeRegistry::reserve(const protocol::JoinRequest& request) {
    if (request.profileId == protocol::kUnassignedProfileId) {
        return ReserveResult{ReserveStatus::InvalidProfileId, kUnprovisionedNodeId};
    }

    NodeRecord* existing = findMutableByUid(request.deviceUid);
    if (existing != nullptr) {
        if (existing->profileId != request.profileId) {
            return ReserveResult{ReserveStatus::ProfileConflict, existing->nodeId};
        }
        if (existing->state == NodeState::Active) {
            return ReserveResult{ReserveStatus::ExistingActive, existing->nodeId};
        }
        if (existing->state == NodeState::Disabled) {
            return ReserveResult{ReserveStatus::ExistingDisabled, existing->nodeId};
        }

        existing->firmware = request.firmware;
        existing->requestNonce = request.requestNonce;
        return ReserveResult{
            ReserveStatus::ExistingPendingUpdated,
            existing->nodeId};
    }

    if (count_ >= kMaxNodes) {
        return ReserveResult{ReserveStatus::Full, kUnprovisionedNodeId};
    }
    const uint8_t nodeId = allocateNodeId();
    if (nodeId == kUnprovisionedNodeId) {
        return ReserveResult{ReserveStatus::Full, kUnprovisionedNodeId};
    }

    NodeRecord& record = records_[count_++];
    for (size_t index = 0; index < protocol::kDeviceUidSize; ++index) {
        record.deviceUid[index] = request.deviceUid[index];
    }
    record.nodeId = nodeId;
    record.profileId = request.profileId;
    record.firmware = request.firmware;
    record.state = NodeState::Pending;
    record.requestNonce = request.requestNonce;
    return ReserveResult{ReserveStatus::Created, nodeId};
}

ConfirmStatus NodeRegistry::confirm(
    const uint8_t* deviceUid,
    const uint8_t nodeId,
    const uint32_t requestNonce) {
    NodeRecord* record = findMutableByNodeId(nodeId);
    if (record == nullptr) {
        return ConfirmStatus::NotFound;
    }
    if (!uidEquals(record->deviceUid, deviceUid)) {
        return ConfirmStatus::IdentityMismatch;
    }
    if (record->requestNonce != requestNonce) {
        return ConfirmStatus::NonceMismatch;
    }
    if (record->state == NodeState::Active) {
        return ConfirmStatus::AlreadyActive;
    }
    if (record->state != NodeState::Pending) {
        return ConfirmStatus::NotPending;
    }

    record->state = NodeState::Active;
    return ConfirmStatus::Confirmed;
}

bool NodeRegistry::disable(const uint8_t nodeId) {
    NodeRecord* record = findMutableByNodeId(nodeId);
    if (record == nullptr || record->state == NodeState::Disabled) {
        return false;
    }
    record->state = NodeState::Disabled;
    record->requestNonce = 0;
    return true;
}

bool NodeRegistry::remove(const uint8_t nodeId) {
    for (size_t index = 0; index < count_; ++index) {
        if (records_[index].nodeId != nodeId) {
            continue;
        }
        records_[index] = records_[count_ - 1];
        records_[count_ - 1] = NodeRecord{};
        --count_;
        return true;
    }
    return false;
}

RenameStatus NodeRegistry::rename(
    const uint8_t nodeId, const char* const displayName, const size_t length) {
    if (!validDisplayName(displayName, length)) return RenameStatus::InvalidName;
    NodeRecord* const record = findMutableByNodeId(nodeId);
    if (record == nullptr) return RenameStatus::NotFound;
    if (record->displayNameLength == length &&
        (length == 0 || memcmp(record->displayName, displayName, length) == 0)) {
        return RenameStatus::NoChange;
    }
    memset(record->displayName, 0, sizeof(record->displayName));
    if (length != 0) memcpy(record->displayName, displayName, length);
    record->displayNameLength = static_cast<uint8_t>(length);
    return RenameStatus::Renamed;
}

bool NodeRegistry::restore(const NodeRecord* records, const size_t count) {
    if ((records == nullptr && count != 0) || count > kMaxNodes) {
        return false;
    }

    for (size_t index = 0; index < count; ++index) {
        const NodeRecord& record = records[index];
        if (record.nodeId < kFirstNodeId || record.nodeId > kLastNodeId ||
            record.profileId == protocol::kUnassignedProfileId ||
            !validDisplayName(record.displayName, record.displayNameLength) ||
            (record.state != NodeState::Pending &&
             record.state != NodeState::Active &&
             record.state != NodeState::Disabled)) {
            return false;
        }
        for (size_t previous = 0; previous < index; ++previous) {
            if (records[previous].nodeId == record.nodeId ||
                uidEquals(records[previous].deviceUid, record.deviceUid)) {
                return false;
            }
        }
    }

    count_ = count;
    for (size_t index = 0; index < count; ++index) {
        records_[index] = records[index];
    }
    for (size_t index = count; index < kMaxNodes; ++index) {
        records_[index] = NodeRecord{};
    }
    return true;
}

}  // namespace registry
}  // namespace radiosensors
