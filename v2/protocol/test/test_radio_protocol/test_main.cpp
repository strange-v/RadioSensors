#include <CommandSessionFrames.h>
#include <JoinRequest.h>
#include <ProfileIds.h>
#include <RadioProtocol.h>
#include <TelemetryFrames.h>
#include <GatewayStream.h>
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
    const uint8_t bytes[] = {0x40, 0xE4, 0x0C, 0x05};
    FrameView frame{};
    TelemetryView telemetry{};

    TEST_ASSERT_EQUAL(
        static_cast<int>(DecodeStatus::Ok),
        static_cast<int>(decodeFrame(bytes, sizeof(bytes), frame)));
    TEST_ASSERT_EQUAL(
        static_cast<int>(FrameKind::Telemetry),
        static_cast<int>(frame.kind));
    TEST_ASSERT_EQUAL_UINT32(3, frame.payloadSize);
    TEST_ASSERT_EQUAL_PTR(bytes + 1, frame.payload);
    TEST_ASSERT_EQUAL(
        static_cast<int>(TelemetryCodecStatus::Ok),
        static_cast<int>(decodeTelemetry(bytes, sizeof(bytes), telemetry)));
    TEST_ASSERT_EQUAL_UINT16(3300, telemetry.supplyMillivolts);
    TEST_ASSERT_EQUAL_UINT32(1, telemetry.profilePayloadSize);
    TEST_ASSERT_EQUAL_PTR(bytes + 3, telemetry.profilePayload);
    TEST_ASSERT_EQUAL_HEX8(0x05, telemetry.profilePayload[0]);
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
    const uint8_t bytes[] = {0x40, 0xE4, 0x0C, 0x2E, 0x09};
    TelemetryView telemetry{};
    TEST_ASSERT_EQUAL(
        static_cast<int>(TelemetryCodecStatus::Ok),
        static_cast<int>(decodeTelemetry(bytes, sizeof(bytes), telemetry)));
    TEST_ASSERT_EQUAL_UINT16(3300, telemetry.supplyMillivolts);
    TEST_ASSERT_EQUAL_UINT32(2, telemetry.profilePayloadSize);
    TEST_ASSERT_EQUAL_HEX16(
        0x092E, readUint16Le(telemetry.profilePayload));
}

void test_initial_telemetry_encoders() {
    uint8_t frame[kBinaryClimateThTelemetrySize]{};

    TEST_ASSERT_EQUAL(
        static_cast<int>(TelemetryCodecStatus::Ok),
        static_cast<int>(encodeBinaryTelemetry(3300, 1, frame, sizeof(frame))));
    const uint8_t binaryExpected[] = {0x40, 0xE4, 0x0C, 0x01};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(binaryExpected, frame, sizeof(binaryExpected));

    TEST_ASSERT_EQUAL(
        static_cast<int>(TelemetryCodecStatus::Ok),
        static_cast<int>(encodeCounterTelemetry(
            3300, 0x12345678UL, frame, sizeof(frame))));
    const uint8_t counterExpected[] = {
        0x40, 0xE4, 0x0C, 0x78, 0x56, 0x34, 0x12};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(counterExpected, frame, sizeof(counterExpected));

    TEST_ASSERT_EQUAL(
        static_cast<int>(TelemetryCodecStatus::Ok),
        static_cast<int>(encodeBinaryClimateThTelemetry(
            3300, 1, 2350, 4567, frame, sizeof(frame))));
    const uint8_t climateExpected[] = {
        0x40, 0xE4, 0x0C, 0x01, 0x2E, 0x09, 0xD7, 0x11};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(climateExpected, frame, sizeof(climateExpected));

    TEST_ASSERT_EQUAL(
        static_cast<int>(TelemetryCodecStatus::Ok),
        static_cast<int>(encodeBinaryTemperatureTelemetry(
            3300, 1, 2350, frame, sizeof(frame))));
    const uint8_t temperatureExpected[] = {
        0x40, 0xE4, 0x0C, 0x01, 0x2E, 0x09};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(
        temperatureExpected, frame, sizeof(temperatureExpected));
}

void test_binary_telemetry_rejects_invalid_state() {
    uint8_t frame[kBinaryTelemetrySize]{};
    TEST_ASSERT_EQUAL(
        static_cast<int>(TelemetryCodecStatus::InvalidState),
        static_cast<int>(encodeBinaryTelemetry(3300, 2, frame, sizeof(frame))));
}

void test_climate_profile_known_vectors() {
    uint8_t th[kClimateThTelemetrySize]{};
    TEST_ASSERT_EQUAL(
        static_cast<int>(TelemetryCodecStatus::Ok),
        static_cast<int>(encodeClimateThTelemetry(
            3300, 2350, 4567, th, sizeof(th))));
    const uint8_t thExpected[] = {
        0x40, 0xE4, 0x0C, 0x2E, 0x09, 0xD7, 0x11};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(thExpected, th, sizeof(th));

    uint8_t thp[kClimateThpTelemetrySize]{};
    TEST_ASSERT_EQUAL(
        static_cast<int>(TelemetryCodecStatus::Ok),
        static_cast<int>(encodeClimateThpTelemetry(
            3300, 2350, 4567, 10132, thp, sizeof(thp))));
    const uint8_t thpExpected[] = {
        0x40, 0xE4, 0x0C, 0x2E, 0x09, 0xD7, 0x11, 0x94, 0x27};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(thpExpected, thp, sizeof(thp));
}

void test_climate_encoders_validate_measurements_and_allow_sentinels() {
    uint8_t frame[kClimateThpTelemetrySize]{};
    TEST_ASSERT_EQUAL(
        static_cast<int>(TelemetryCodecStatus::InvalidTemperature),
        static_cast<int>(encodeClimateThTelemetry(
            3300, -8001, 5000, frame, sizeof(frame))));
    TEST_ASSERT_EQUAL(
        static_cast<int>(TelemetryCodecStatus::InvalidHumidity),
        static_cast<int>(encodeClimateThTelemetry(
            3300, 2000, 10001, frame, sizeof(frame))));
    TEST_ASSERT_EQUAL(
        static_cast<int>(TelemetryCodecStatus::InvalidPressure),
        static_cast<int>(encodeClimateThpTelemetry(
            3300, 2000, 5000, 2999, frame, sizeof(frame))));
    TEST_ASSERT_EQUAL(
        static_cast<int>(TelemetryCodecStatus::Ok),
        static_cast<int>(encodeClimateThpTelemetry(
            kInvalidSupplyVoltage, kInvalidTemperature, kInvalidHumidity,
            kInvalidPressure, frame, sizeof(frame))));
}

void test_telemetry_prefix_rejects_wrong_length_and_header() {
    TelemetryView telemetry{};
    const uint8_t tooShort[] = {0x40, 0xE4};
    TEST_ASSERT_EQUAL(
        static_cast<int>(TelemetryCodecStatus::WrongLength),
        static_cast<int>(decodeTelemetry(
            tooShort, sizeof(tooShort), telemetry)));

    const uint8_t wrongKind[] = {0x41, 0xE4, 0x0C};
    TEST_ASSERT_EQUAL(
        static_cast<int>(TelemetryCodecStatus::WrongHeader),
        static_cast<int>(decodeTelemetry(
            wrongKind, sizeof(wrongKind), telemetry)));
}

void test_gateway_stream_known_vectors() {
    uint8_t control[radiosensors::stream::kControlFrameSize]{};
    TEST_ASSERT_EQUAL_UINT32(
        sizeof(control),
        radiosensors::stream::encodeControl(
            radiosensors::stream::MessageKind::SnapshotBegin,
            0x78563412,
            control,
            sizeof(control)));
    const uint8_t expectedControl[] = {1, 1, 0x12, 0x34, 0x56, 0x78};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expectedControl, control, sizeof(control));

    const uint8_t payload[] = {0x40, 0xE7, 0x0C};
    uint8_t telemetry[radiosensors::stream::kTelemetryEnvelopeSize + sizeof(payload)]{};
    TEST_ASSERT_EQUAL_UINT32(
        sizeof(telemetry),
        radiosensors::stream::encodeTelemetry(
            0x01020304,
            7,
            0x1234,
            0x0102030411223344ULL,
            -63,
            payload,
            sizeof(payload),
            telemetry,
            sizeof(telemetry)));
    const uint8_t expectedTelemetry[] = {
        1, 2, 4, 3, 2, 1, 7, 0x34, 0x12,
        0x44, 0x33, 0x22, 0x11, 0x04, 0x03, 0x02, 0x01,
        0xC1, 0xFF, 3, 0x40, 0xE7, 0x0C};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(
        expectedTelemetry, telemetry, sizeof(telemetry));
}

void test_gateway_stream_registry_changed_known_vector() {
    uint8_t frame[radiosensors::stream::kRegistryChangedFrameSize]{};
    TEST_ASSERT_EQUAL_UINT32(
        sizeof(frame),
        radiosensors::stream::encodeRegistryChanged(
            0x01020304, 0x0000002A, frame, sizeof(frame)));
    const uint8_t expected[] = {
        1, 4, 0x04, 0x03, 0x02, 0x01, 0x2A, 0x00, 0x00, 0x00};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, frame, sizeof(frame));
}

void test_gateway_stream_registry_changed_rejects_small_buffer() {
    uint8_t frame[radiosensors::stream::kRegistryChangedFrameSize - 1]{};
    TEST_ASSERT_EQUAL_UINT32(
        0,
        radiosensors::stream::encodeRegistryChanged(
            1, 1, frame, sizeof(frame)));
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
    RUN_TEST(test_climate_profile_known_vectors);
    RUN_TEST(test_climate_encoders_validate_measurements_and_allow_sentinels);
    RUN_TEST(test_telemetry_prefix_rejects_wrong_length_and_header);
    RUN_TEST(test_gateway_stream_known_vectors);
    RUN_TEST(test_gateway_stream_registry_changed_known_vector);
    RUN_TEST(test_gateway_stream_registry_changed_rejects_small_buffer);
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
