#include "NodeRegistry.h"

namespace radiosensors {
namespace registry {

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

bool NodeRegistry::restore(const NodeRecord* records, const size_t count) {
    if ((records == nullptr && count != 0) || count > kMaxNodes) {
        return false;
    }

    for (size_t index = 0; index < count; ++index) {
        const NodeRecord& record = records[index];
        if (record.nodeId < kFirstNodeId || record.nodeId > kLastNodeId ||
            record.profileId == protocol::kUnassignedProfileId ||
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
