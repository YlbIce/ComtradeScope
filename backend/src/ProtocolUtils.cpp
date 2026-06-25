#include "ProtocolUtils.h"

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <sstream>

#ifdef _WIN32
#include <Windows.h>
#endif

namespace protocol {

namespace {

std::string wideToUtf8(const wchar_t* data, int length) {
  if (data == nullptr || length <= 0) {
    return {};
  }
  const int bytes = WideCharToMultiByte(CP_UTF8, 0, data, length, nullptr, 0, nullptr, nullptr);
  if (bytes <= 0) {
    return {};
  }
  std::string result(static_cast<std::size_t>(bytes), '\0');
  WideCharToMultiByte(CP_UTF8, 0, data, length, result.data(), bytes, nullptr, nullptr);
  return result;
}

std::string escapeByte(std::uint8_t byte) {
  char buffer[5] = {};
  std::snprintf(buffer, sizeof(buffer), "\\x%02X", byte);
  return buffer;
}

} // namespace

std::string bytesToHex(const Bytes& data) {
  std::ostringstream stream;
  stream << std::uppercase << std::hex << std::setfill('0');
  for (std::size_t i = 0; i < data.size(); ++i) {
    if (i != 0) {
      stream << ' ';
    }
    stream << std::setw(2) << static_cast<int>(data[i]);
  }
  return stream.str();
}

bool hexToBytes(const std::string& input, Bytes& output, std::string& error) {
  std::string normalized;
  normalized.reserve(input.size());
  for (std::size_t i = 0; i < input.size(); ++i) {
    const unsigned char ch = static_cast<unsigned char>(input[i]);
    if (std::isspace(ch) || ch == ',' || ch == ';' || ch == ':' || ch == '_' || ch == '-') {
      continue;
    }
    if (ch == '0' && i + 1 < input.size() && (input[i + 1] == 'x' || input[i + 1] == 'X')) {
      ++i;
      continue;
    }
    normalized.push_back(static_cast<char>(ch));
  }

  if (normalized.empty()) {
    output.clear();
    return true;
  }

  if ((normalized.size() % 2) != 0) {
    error = "HEX 长度必须是偶数";
    return false;
  }

  Bytes bytes;
  bytes.reserve(normalized.size() / 2);
  for (std::size_t i = 0; i < normalized.size(); i += 2) {
    const std::string part = normalized.substr(i, 2);
    char* end = nullptr;
    const long value = std::strtol(part.c_str(), &end, 16);
    if (end == nullptr || *end != '\0' || value < 0 || value > 255) {
      error = "非法 HEX 字符：" + part;
      return false;
    }
    bytes.push_back(static_cast<std::uint8_t>(value));
  }

  output = std::move(bytes);
  return true;
}

Bytes textToBytes(const std::string& text, const std::string& lineEnding) {
  Bytes data(text.begin(), text.end());
  if (lineEnding == "CR") {
    data.push_back('\r');
  } else if (lineEnding == "LF") {
    data.push_back('\n');
  } else if (lineEnding == "CRLF") {
    data.push_back('\r');
    data.push_back('\n');
  }
  return data;
}

std::uint16_t crc16Modbus(const Bytes& data) {
  std::uint16_t crc = 0xFFFF;
  for (const auto byte : data) {
    crc ^= byte;
    for (int bit = 0; bit < 8; ++bit) {
      if ((crc & 0x0001) != 0) {
        crc = static_cast<std::uint16_t>((crc >> 1) ^ 0xA001);
      } else {
        crc = static_cast<std::uint16_t>(crc >> 1);
      }
    }
  }
  return crc;
}

void appendModbusCrc(Bytes& data) {
  const std::uint16_t crc = crc16Modbus(data);
  data.push_back(static_cast<std::uint8_t>(crc & 0xFF));
  data.push_back(static_cast<std::uint8_t>((crc >> 8) & 0xFF));
}

std::string bytesToDisplayText(const Bytes& data) {
  std::string result;
  result.reserve(data.size());
  for (const auto byte : data) {
    if (byte == '\r') {
      result += "\\r";
    } else if (byte == '\n') {
      result += "\\n";
    } else if (byte == '\t') {
      result += "\\t";
    } else if (byte >= 0x20 && byte <= 0x7E) {
      result.push_back(static_cast<char>(byte));
    } else {
      result += escapeByte(byte);
    }
  }
  return result;
}

std::string nativeToUtf8(const std::string& value) {
  if (value.empty()) {
    return {};
  }

#ifdef _WIN32
  const int wideChars = MultiByteToWideChar(CP_ACP, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
  if (wideChars > 0) {
    std::wstring wide(static_cast<std::size_t>(wideChars), L'\0');
    MultiByteToWideChar(CP_ACP, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), wide.data(), wideChars);
    return wideToUtf8(wide.data(), wideChars);
  }
#endif

  return sanitizeUtf8(value);
}

std::string sanitizeUtf8(const std::string& value) {
  std::string result;
  result.reserve(value.size());

  for (std::size_t i = 0; i < value.size();) {
    const auto byte = static_cast<std::uint8_t>(value[i]);
    if (byte <= 0x7F) {
      if (byte >= 0x20 || byte == '\r' || byte == '\n' || byte == '\t') {
        result.push_back(static_cast<char>(byte));
      } else {
        result += escapeByte(byte);
      }
      ++i;
      continue;
    }

    std::size_t length = 0;
    std::uint32_t codepoint = 0;
    if ((byte & 0xE0) == 0xC0) {
      length = 2;
      codepoint = byte & 0x1F;
    } else if ((byte & 0xF0) == 0xE0) {
      length = 3;
      codepoint = byte & 0x0F;
    } else if ((byte & 0xF8) == 0xF0) {
      length = 4;
      codepoint = byte & 0x07;
    } else {
      result += escapeByte(byte);
      ++i;
      continue;
    }

    if (i + length > value.size()) {
      result += escapeByte(byte);
      ++i;
      continue;
    }

    bool valid = true;
    for (std::size_t j = 1; j < length; ++j) {
      const auto next = static_cast<std::uint8_t>(value[i + j]);
      if ((next & 0xC0) != 0x80) {
        valid = false;
        break;
      }
      codepoint = (codepoint << 6) | (next & 0x3F);
    }

    if (!valid
        || (length == 2 && codepoint < 0x80)
        || (length == 3 && codepoint < 0x800)
        || (length == 4 && codepoint < 0x10000)
        || codepoint > 0x10FFFF
        || (codepoint >= 0xD800 && codepoint <= 0xDFFF)) {
      result += escapeByte(byte);
      ++i;
      continue;
    }

    result.append(value, i, length);
    i += length;
  }

  return result;
}

std::string utcTimestamp() {
  const auto now = std::chrono::system_clock::now();
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
  const std::time_t time = std::chrono::system_clock::to_time_t(now);
  std::tm tm {};
  gmtime_s(&tm, &time);

  std::ostringstream stream;
  stream << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S")
         << '.' << std::setw(3) << std::setfill('0') << ms.count() << 'Z';
  return stream.str();
}

Json makeError(const std::string& requestId, const std::string& message) {
  return {
    {"type", "error"},
    {"requestId", requestId},
    {"payload", {{"message", message}}}
  };
}

Json makeOk(const std::string& requestId, const std::string& type, const Json& payload) {
  return {
    {"type", type},
    {"requestId", requestId},
    {"payload", payload}
  };
}

} // namespace protocol
