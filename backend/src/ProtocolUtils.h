#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace protocol {

using Bytes = std::vector<std::uint8_t>;
using Json = nlohmann::json;

std::string bytesToHex(const Bytes& data);
bool hexToBytes(const std::string& input, Bytes& output, std::string& error);
Bytes textToBytes(const std::string& text, const std::string& lineEnding);
std::uint16_t crc16Modbus(const Bytes& data);
void appendModbusCrc(Bytes& data);
std::string bytesToDisplayText(const Bytes& data);
std::string nativeToUtf8(const std::string& value);
std::string sanitizeUtf8(const std::string& value);
std::string utcTimestamp();

Json makeError(const std::string& requestId, const std::string& message);
Json makeOk(const std::string& requestId, const std::string& type, const Json& payload = Json::object());

} // namespace protocol
