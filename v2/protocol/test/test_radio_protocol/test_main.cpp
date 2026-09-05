#include <CommandSessionFrames.h>
#include <JoinRequest.h>
#include <ProfileIds.h>
#include <RadioProtocol.h>
#include <TelemetryFrames.h>
#include <unity.h>

using namespace radiosensors::protocol;

void setUp() {}
void tearDown() {}

void test_header_bit_layout() {
    TEST_ASSERT_EQUAL_HEX8(0x40, encodeHeader(FrameKind::Telemetry));
    TEST_ASSERT_EQUAL_HEX8(0x41, encodeHeader(FrameKind::JoinRequest));
    TEST_ASSERT_EQUAL_HEX8(0x46, encodeHeader(FrameKind::Error));
    TEST_ASSERT_EQUAL_HEX8(0x48, encodeHeader(FrameKind::CommandReady));
    TEST_ASSERT_EQUAL_HEX8(0x49, encodeHeader(FrameKind::NoCommand));
}

void test_command_session_known_vectors() {
    const uint8_t readyExpected[] = {0x48, 0xEF, 0xCD, 0xAB, 0x89};
    const uint8_t noneExpected[] = {0x49, 0xEF, 0xCD, 0xAB, 0x89};
    uint8_t encoded[kCommandReadySize]{};
    uint32_t nonce = 0;

    TEST_ASSERT_EQUAL(
        static_cast<int>(CommandSessionCodecStatus::Ok),
        static_cast<int>(encodeCommandReady(
            0x89ABCDEFUL, encoded, sizeof(encoded))));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(readyExpected, encoded, sizeof(encoded));
    TEST_ASSERT_EQUAL(
        static_cast<int>(CommandSessionCodecStatus::Ok),
        static_cast<int>(decodeCommandReady(encoded, sizeof(encoded), nonce)));
    TEST_ASSERT_EQUAL_HEX32(0x89ABCDEFUL, nonce);

    TEST_ASSERT_EQUAL(
        static_cast<int>(CommandSessionCodecStatus::Ok),
        static_cast<int>(encodeNoCommand(
            0x89ABCDEFUL, encoded, sizeof(encoded))));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(noneExpected, encoded, sizeof(encoded));
    TEST_ASSERT_EQUAL(
        static_cast<int>(CommandSessionCodecStatus::Ok),
        static_cast<int>(decodeNoCommand(encoded, sizeof(encoded), nonce)));
    TEST_ASSERT_EQUAL_HEX32(0x89ABCDEFUL, nonce);
}

void test_initial_profile_ids_are_stable() {
    TEST_ASSERT_EQUAL_UINT16(1, profileIdValue(ProfileId::Voltage));
    TEST_ASSERT_EQUAL_UINT16(2, profileIdValue(ProfileId::Temperature));
    TEST_ASSERT_EQUAL_UINT16(3, profileIdValue(ProfileId::ClimateTh));
    TEST_ASSERT_EQUAL_UINT16(4, profileIdValue(ProfileId::ClimateThp));
    TEST_ASSERT_EQUAL_UINT16(5, profileIdValue(ProfileId::Binary));
    TEST_ASSERT_EQUAL_UINT16(6, profileIdValue(ProfileId::PulseCounter));
    TEST_ASSERT_EQUAL_UINT16(7, profileIdValue(ProfileId::BinaryClimateTh));
    TEST_ASSERT_EQUAL_UINT16(8, profileIdValue(ProfileId::BinaryTemperature));
}

void test_decodes_opaque_telemetry() {
    const uint8_t bytes[] = {0x40, 0x34, 0x12, 0x05};
    FrameView frame{};

    TEST_ASSERT_EQUAL(
        static_cast<int>(DecodeStatus::Ok),
        static_cast<int>(decodeFrame(bytes, sizeof(bytes), frame)));
    TEST_ASSERT_EQUAL(
        static_cast<int>(FrameKind::Telemetry),
        static_cast<int>(frame.kind));
    TEST_ASSERT_EQUAL_UINT32(3, frame.payloadSize);
    TEST_ASSERT_EQUAL_PTR(bytes + 1, frame.payload);
    TEST_ASSERT_EQUAL_HEX8(0x34, frame.payload[0]);
}

void test_profile_1_known_vector() {
    const uint8_t bytes[] = {0x40, 0xE4, 0x0C};
    FrameView frame{};
    TEST_ASSERT_EQUAL(
        static_cast<int>(DecodeStatus::Ok),
        static_cast<int>(decodeFrame(bytes, sizeof(bytes), frame)));
    TEST_ASSERT_EQUAL_UINT32(2, frame.payloadSize);
    TEST_ASSERT_EQUAL_UINT16(3300, readUint16Le(frame.payload));
}

void test_profile_2_known_vector() {
    const uint8_t bytes[] = {0x40, 0x2E, 0x09, 0xE4, 0x0C};
    FrameView frame{};
    TEST_ASSERT_EQUAL(
        static_cast<int>(DecodeStatus::Ok),
        static_cast<int>(decodeFrame(bytes, sizeof(bytes), frame)));
    TEST_ASSERT_EQUAL_UINT32(4, frame.payloadSize);
    TEST_ASSERT_EQUAL_HEX16(0x092E, readUint16Le(frame.payload));
    TEST_ASSERT_EQUAL_UINT16(3300, readUint16Le(frame.payload + 2));
}

void test_initial_telemetry_encoders() {
    uint8_t frame[kBinaryClimateThTelemetrySize]{};

    TEST_ASSERT_EQUAL(
        static_cast<int>(TelemetryCodecStatus::Ok),
        static_cast<int>(encodeBinaryTelemetry(1, 3300, frame, sizeof(frame))));
    const uint8_t binaryExpected[] = {0x40, 0x01, 0xE4, 0x0C};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(binaryExpected, frame, sizeof(binaryExpected));

    TEST_ASSERT_EQUAL(
        static_cast<int>(TelemetryCodecStatus::Ok),
        static_cast<int>(encodeCounterTelemetry(
            0x12345678UL, 3300, frame, sizeof(frame))));
    const uint8_t counterExpected[] = {
        0x40, 0x78, 0x56, 0x34, 0x12, 0xE4, 0x0C};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(counterExpected, frame, sizeof(counterExpected));

    TEST_ASSERT_EQUAL(
        static_cast<int>(TelemetryCodecStatus::Ok),
        static_cast<int>(encodeBinaryClimateThTelemetry(
            1, 2350, 4567, 3300, frame, sizeof(frame))));
    const uint8_t climateExpected[] = {
        0x40, 0x01, 0x2E, 0x09, 0xD7, 0x11, 0xE4, 0x0C};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(climateExpected, frame, sizeof(climateExpected));

    TEST_ASSERT_EQUAL(
        static_cast<int>(TelemetryCodecStatus::Ok),
        static_cast<int>(encodeBinaryTemperatureTelemetry(
            1, 2350, 3300, frame, sizeof(frame))));
    const uint8_t temperatureExpected[] = {
        0x40, 0x01, 0x2E, 0x09, 0xE4, 0x0C};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(
        temperatureExpected, frame, sizeof(temperatureExpected));
}

void test_binary_telemetry_rejects_invalid_state() {
    uint8_t frame[kBinaryTelemetrySize]{};
    TEST_ASSERT_EQUAL(
        static_cast<int>(TelemetryCodecStatus::InvalidState),
        static_cast<int>(encodeBinaryTelemetry(2, 3300, frame, sizeof(frame))));
}

void test_allows_empty_payload_for_kind_specific_validation() {
    const uint8_t bytes[] = {0x43};
    FrameView frame{};

    TEST_ASSERT_EQUAL(
        static_cast<int>(DecodeStatus::Ok),
        static_cast<int>(decodeFrame(bytes, sizeof(bytes), frame)));
    TEST_ASSERT_EQUAL_UINT32(0, frame.payloadSize);
}

void test_rejects_empty_frame() {
    FrameView frame{};
    TEST_ASSERT_EQUAL(
        static_cast<int>(DecodeStatus::EmptyFrame),
        static_cast<int>(decodeFrame(nullptr, 0, frame)));
}

void test_rejects_other_protocol_major() {
    const uint8_t bytes[] = {0x20};
    FrameView frame{};
    TEST_ASSERT_EQUAL(
        static_cast<int>(DecodeStatus::UnsupportedVersion),
        static_cast<int>(decodeFrame(bytes, sizeof(bytes), frame)));
}

void test_rejects_reserved_kind() {
    const uint8_t bytes[] = {0x5F};
    FrameView frame{};
    TEST_ASSERT_EQUAL(
        static_cast<int>(DecodeStatus::UnsupportedKind),
        static_cast<int>(decodeFrame(bytes, sizeof(bytes), frame)));
}

void test_encodes_join_request_known_vector() {
    const JoinRequest request{
        {0x10, 0x21, 0x32, 0x43, 0x54, 0x65, 0x76, 0x87, 0x98, 0xA9},
        0x1234,
        {1, 2, 3},
        0x89ABCDEF,
    };
    const uint8_t expected[kJoinRequestSize] = {
        0x41,
        0x10, 0x21, 0x32, 0x43, 0x54, 0x65, 0x76, 0x87, 0x98, 0xA9,
        0x34, 0x12,
        0x01, 0x02, 0x03,
        0xEF, 0xCD, 0xAB, 0x89,
    };
    uint8_t encoded[kJoinRequestSize]{};

    TEST_ASSERT_EQUAL(
        static_cast<int>(JoinRequestStatus::Ok),
        static_cast<int>(encodeJoinRequest(request, encoded, sizeof(encoded))));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, encoded, kJoinRequestSize);
}

void test_decodes_join_request_known_vector() {
    const uint8_t encoded[kJoinRequestSize] = {
        0x41,
        0x10, 0x21, 0x32, 0x43, 0x54, 0x65, 0x76, 0x87, 0x98, 0xA9,
        0x34, 0x12,
        0x01, 0x02, 0x03,
        0xEF, 0xCD, 0xAB, 0x89,
    };
    JoinRequest request{};

    TEST_ASSERT_EQUAL(
        static_cast<int>(JoinRequestStatus::Ok),
        static_cast<int>(decodeJoinRequest(encoded, sizeof(encoded), request)));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(encoded + 1, request.deviceUid, kDeviceUidSize);
    TEST_ASSERT_EQUAL_HEX16(0x1234, request.profileId);
    TEST_ASSERT_EQUAL_UINT8(1, request.firmware.major);
    TEST_ASSERT_EQUAL_UINT8(2, request.firmware.minor);
    TEST_ASSERT_EQUAL_UINT8(3, request.firmware.patch);
    TEST_ASSERT_EQUAL_HEX32(0x89ABCDEF, request.requestNonce);
}

void test_join_request_requires_exact_length() {
    uint8_t encoded[kJoinRequestSize + 1] = {0x41};
    encoded[11] = 1;
    JoinRequest request{};

    TEST_ASSERT_EQUAL(
        static_cast<int>(JoinRequestStatus::WrongLength),
        static_cast<int>(decodeJoinRequest(encoded, sizeof(encoded), request)));
}

void test_join_request_rejects_profile_zero() {
    uint8_t encoded[kJoinRequestSize] = {0x41};
    JoinRequest request{};

    TEST_ASSERT_EQUAL(
        static_cast<int>(JoinRequestStatus::InvalidProfileId),
        static_cast<int>(decodeJoinRequest(encoded, sizeof(encoded), request)));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_header_bit_layout);
    RUN_TEST(test_command_session_known_vectors);
    RUN_TEST(test_initial_profile_ids_are_stable);
    RUN_TEST(test_decodes_opaque_telemetry);
    RUN_TEST(test_profile_1_known_vector);
    RUN_TEST(test_profile_2_known_vector);
    RUN_TEST(test_initial_telemetry_encoders);
    RUN_TEST(test_binary_telemetry_rejects_invalid_state);
    RUN_TEST(test_allows_empty_payload_for_kind_specific_validation);
    RUN_TEST(test_rejects_empty_frame);
    RUN_TEST(test_rejects_other_protocol_major);
    RUN_TEST(test_rejects_reserved_kind);
    RUN_TEST(test_encodes_join_request_known_vector);
    RUN_TEST(test_decodes_join_request_known_vector);
    RUN_TEST(test_join_request_requires_exact_length);
    RUN_TEST(test_join_request_rejects_profile_zero);
    return UNITY_END();
}
