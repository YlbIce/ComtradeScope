#include "ComtradeParser.h"

#include <algorithm>
#include <bit>
#include <cctype>
#include <cmath>
#include <fstream>
#include <iterator>
#include <sstream>
#include <stdexcept>

namespace comtrade {

namespace {

std::string trim(std::string value) {
  auto notSpace = [](unsigned char ch) { return !std::isspace(ch); };
  value.erase(value.begin(), std::find_if(value.begin(), value.end(), notSpace));
  value.erase(std::find_if(value.rbegin(), value.rend(), notSpace).base(), value.end());
  return value;
}

std::string upper(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
    return static_cast<char>(std::toupper(ch));
  });
  return value;
}

std::vector<std::string> splitCsvLine(const std::string& line) {
  std::vector<std::string> parts;
  std::string current;
  bool quoted = false;
  for (char ch : line) {
    if (ch == '"') {
      quoted = !quoted;
      continue;
    }
    if (ch == ',' && !quoted) {
      parts.push_back(trim(current));
      current.clear();
      continue;
    }
    current.push_back(ch);
  }
  parts.push_back(trim(current));
  return parts;
}

double toDouble(const std::vector<std::string>& parts, std::size_t index, double fallback = 0.0) {
  if (index >= parts.size() || parts[index].empty()) {
    return fallback;
  }
  try {
    return std::stod(parts[index]);
  } catch (...) {
    return fallback;
  }
}

int toInt(const std::vector<std::string>& parts, std::size_t index, int fallback = 0) {
  if (index >= parts.size() || parts[index].empty()) {
    return fallback;
  }
  try {
    return std::stoi(parts[index]);
  } catch (...) {
    return fallback;
  }
}

std::int64_t toInt64(const std::vector<std::string>& parts, std::size_t index, std::int64_t fallback = 0) {
  if (index >= parts.size() || parts[index].empty()) {
    return fallback;
  }
  try {
    return std::stoll(parts[index]);
  } catch (...) {
    return fallback;
  }
}

std::filesystem::path findDatPath(const std::filesystem::path& cfgPath) {
  auto dat = cfgPath;
  dat.replace_extension(".dat");
  if (std::filesystem::exists(dat)) {
    return dat;
  }

  dat.replace_extension(".DAT");
  if (std::filesystem::exists(dat)) {
    return dat;
  }

  return {};
}

template <typename T>
T readLittleEndian(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
  if (offset + sizeof(T) > bytes.size()) {
    throw std::runtime_error("DAT 二进制数据长度不足");
  }
  T value {};
  std::copy_n(bytes.begin() + static_cast<std::ptrdiff_t>(offset), sizeof(T), reinterpret_cast<std::uint8_t*>(&value));
  return value;
}

double scaleAnalog(const AnalogChannel& channel, double raw) {
  return raw * channel.multiplier + channel.adder;
}

int digitalBit(const std::vector<std::uint8_t>& bytes, std::size_t offset, std::size_t bitIndex) {
  const std::size_t wordOffset = offset + (bitIndex / 16) * 2;
  if (wordOffset + 1 >= bytes.size()) {
    return 0;
  }
  const auto word = static_cast<std::uint16_t>(bytes[wordOffset] | (bytes[wordOffset + 1] << 8));
  return (word & (1u << (bitIndex % 16))) ? 1 : 0;
}

double fallbackTimeSeconds(const ComtradeRecord& record, std::int64_t sampleIndex) {
  if (record.sampleRates.empty() || record.sampleRates.front().rate <= 0.0) {
    return static_cast<double>(sampleIndex - 1);
  }
  return static_cast<double>(sampleIndex - 1) / record.sampleRates.front().rate;
}

} // namespace

Json toJson(const AnalogChannel& channel) {
  return {
    {"kind", "analog"},
    {"index", channel.index},
    {"id", channel.id},
    {"phase", channel.phase},
    {"circuit", channel.circuit},
    {"unit", channel.unit},
    {"multiplier", channel.multiplier},
    {"adder", channel.adder},
    {"skew", channel.skew},
    {"primary", channel.primary},
    {"secondary", channel.secondary},
    {"scaling", channel.scaling}
  };
}

Json toJson(const DigitalChannel& channel) {
  return {
    {"kind", "digital"},
    {"index", channel.index},
    {"id", channel.id},
    {"phase", channel.phase},
    {"circuit", channel.circuit},
    {"normalState", channel.normalState}
  };
}

Json toJson(const SampleRateBlock& block) {
  return {
    {"rate", block.rate},
    {"endSample", block.endSample}
  };
}

ComtradeRecord ComtradeParser::load(const std::filesystem::path& cfgPath, const std::filesystem::path& datPath) {
  ComtradeRecord record;
  parseCfg(record, cfgPath);

  const auto resolvedDatPath = datPath.empty() ? findDatPath(cfgPath) : datPath;
  if (resolvedDatPath.empty()) {
    throw std::runtime_error("未找到与 CFG 同名的 DAT 文件，请手动选择 DAT 文件");
  }
  parseDat(record, resolvedDatPath);
  return record;
}

void ComtradeParser::parseCfg(ComtradeRecord& record, const std::filesystem::path& cfgPath) {
  std::ifstream input(cfgPath);
  if (!input) {
    throw std::runtime_error("无法打开 CFG 文件：" + cfgPath.string());
  }

  record.cfgPath = cfgPath.string();

  std::vector<std::string> lines;
  std::string line;
  while (std::getline(input, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    if (!trim(line).empty()) {
      lines.push_back(line);
    }
  }

  if (lines.size() < 6) {
    throw std::runtime_error("CFG 文件行数不足，无法识别为 COMTRADE 配置");
  }

  std::size_t cursor = 0;
  const auto header = splitCsvLine(lines[cursor++]);
  record.stationName = header.size() > 0 ? header[0] : "";
  record.deviceId = header.size() > 1 ? header[1] : "";
  record.revisionYear = header.size() > 2 ? header[2] : "1999";

  const auto countLine = splitCsvLine(lines[cursor++]);
  const int analogCount = countLine.size() > 1 && upper(countLine[1]).ends_with('A')
    ? std::max(0, toInt(countLine, 1))
    : 0;
  const int digitalCount = countLine.size() > 2 && upper(countLine[2]).ends_with('D')
    ? std::max(0, toInt(countLine, 2))
    : 0;

  for (int i = 0; i < analogCount && cursor < lines.size(); ++i, ++cursor) {
    const auto parts = splitCsvLine(lines[cursor]);
    AnalogChannel channel;
    channel.index = toInt(parts, 0, i + 1);
    channel.id = parts.size() > 1 ? parts[1] : ("A" + std::to_string(i + 1));
    channel.phase = parts.size() > 2 ? parts[2] : "";
    channel.circuit = parts.size() > 3 ? parts[3] : "";
    channel.unit = parts.size() > 4 ? parts[4] : "";
    channel.multiplier = toDouble(parts, 5, 1.0);
    channel.adder = toDouble(parts, 6, 0.0);
    channel.skew = toDouble(parts, 7, 0.0);
    channel.minRaw = toDouble(parts, 8, 0.0);
    channel.maxRaw = toDouble(parts, 9, 0.0);
    channel.primary = toDouble(parts, 10, 1.0);
    channel.secondary = toDouble(parts, 11, 1.0);
    channel.scaling = parts.size() > 12 ? parts[12] : "P";
    record.analogChannels.push_back(std::move(channel));
  }

  for (int i = 0; i < digitalCount && cursor < lines.size(); ++i, ++cursor) {
    const auto parts = splitCsvLine(lines[cursor]);
    DigitalChannel channel;
    channel.index = toInt(parts, 0, i + 1);
    channel.id = parts.size() > 1 ? parts[1] : ("D" + std::to_string(i + 1));
    channel.phase = parts.size() > 2 ? parts[2] : "";
    channel.circuit = parts.size() > 3 ? parts[3] : "";
    channel.normalState = toInt(parts, 4, 0);
    record.digitalChannels.push_back(std::move(channel));
  }

  if (cursor >= lines.size()) {
    throw std::runtime_error("CFG 缺少系统频率");
  }
  record.frequency = trim(lines[cursor++]);

  if (cursor >= lines.size()) {
    throw std::runtime_error("CFG 缺少采样率段数量");
  }
  const int rateCount = std::max(0, toInt(splitCsvLine(lines[cursor++]), 0));
  for (int i = 0; i < rateCount && cursor < lines.size(); ++i, ++cursor) {
    const auto parts = splitCsvLine(lines[cursor]);
    record.sampleRates.push_back({
      toDouble(parts, 0, 0.0),
      toInt64(parts, 1, 0)
    });
  }

  if (record.sampleRates.empty()) {
    record.sampleRates.push_back({0.0, 0});
    record.warnings.push_back("CFG 未提供有效采样率，时间轴将使用样本序号代替");
  }

  if (cursor < lines.size()) {
    record.startTime = trim(lines[cursor++]);
  }
  if (cursor < lines.size()) {
    record.triggerTime = trim(lines[cursor++]);
  }
  if (cursor < lines.size()) {
    record.dataFileType = upper(trim(lines[cursor++]));
  } else {
    record.dataFileType = "ASCII";
  }
  if (cursor < lines.size()) {
    record.timestampMultiplier = trim(lines[cursor++]);
  }

  if (record.dataFileType.empty()) {
    record.dataFileType = "ASCII";
  }
}

void ComtradeParser::parseDat(ComtradeRecord& record, const std::filesystem::path& datPath) {
  record.datPath = datPath.string();
  const auto type = upper(record.dataFileType);
  if (type == "ASCII") {
    parseAsciiDat(record, datPath);
    return;
  }
  if (type == "BINARY") {
    parseBinaryDat(record, datPath, 2, false);
    return;
  }
  if (type == "BINARY32") {
    parseBinaryDat(record, datPath, 4, false);
    return;
  }
  if (type == "FLOAT32" || type == "FLOAT") {
    parseBinaryDat(record, datPath, 4, true);
    return;
  }

  throw std::runtime_error("暂不支持的 DAT 类型：" + record.dataFileType);
}

void ComtradeParser::parseAsciiDat(ComtradeRecord& record, const std::filesystem::path& datPath) {
  std::ifstream input(datPath);
  if (!input) {
    throw std::runtime_error("无法打开 DAT 文件：" + datPath.string());
  }

  const std::size_t analogCount = record.analogChannels.size();
  const std::size_t digitalCount = record.digitalChannels.size();
  std::string line;
  while (std::getline(input, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    if (trim(line).empty()) {
      continue;
    }

    const auto parts = splitCsvLine(line);
    if (parts.size() < 2 + analogCount) {
      record.warnings.push_back("DAT ASCII 行字段不足，已跳过：" + line);
      continue;
    }

    Sample sample;
    sample.index = toInt64(parts, 0, static_cast<std::int64_t>(record.samples.size() + 1));
    // COMTRADE 时间戳通常是微秒；这里统一转成秒，前端只处理秒轴。
    sample.timeSeconds = toDouble(parts, 1, fallbackTimeSeconds(record, sample.index)) / 1'000'000.0;
    sample.analog.reserve(analogCount);
    for (std::size_t i = 0; i < analogCount; ++i) {
      const double raw = toDouble(parts, 2 + i, 0.0);
      sample.analog.push_back(scaleAnalog(record.analogChannels[i], raw));
    }
    sample.digital.reserve(digitalCount);
    for (std::size_t i = 0; i < digitalCount; ++i) {
      sample.digital.push_back(toInt(parts, 2 + analogCount + i, 0) != 0 ? 1 : 0);
    }
    record.samples.push_back(std::move(sample));
  }
}

void ComtradeParser::parseBinaryDat(
  ComtradeRecord& record,
  const std::filesystem::path& datPath,
  int analogBytes,
  bool analogFloat
) {
  std::ifstream input(datPath, std::ios::binary);
  if (!input) {
    throw std::runtime_error("无法打开 DAT 文件：" + datPath.string());
  }

  const std::vector<std::uint8_t> bytes{
    std::istreambuf_iterator<char>(input),
    std::istreambuf_iterator<char>()
  };

  const std::size_t analogCount = record.analogChannels.size();
  const std::size_t digitalCount = record.digitalChannels.size();
  const std::size_t digitalWords = (digitalCount + 15) / 16;
  const std::size_t sampleBytes = 4 + 4 + analogCount * static_cast<std::size_t>(analogBytes) + digitalWords * 2;
  if (sampleBytes == 0) {
    return;
  }
  if (bytes.size() % sampleBytes != 0) {
    record.warnings.push_back("DAT 二进制长度不是单样本长度整数倍，尾部残余字节已忽略");
  }

  for (std::size_t offset = 0; offset + sampleBytes <= bytes.size(); offset += sampleBytes) {
    Sample sample;
    sample.index = readLittleEndian<std::uint32_t>(bytes, offset);
    // 二进制 DAT 时间戳单位同样按微秒处理。
    const auto timeRaw = readLittleEndian<std::uint32_t>(bytes, offset + 4);
    sample.timeSeconds = static_cast<double>(timeRaw) / 1'000'000.0;

    std::size_t cursor = offset + 8;
    sample.analog.reserve(analogCount);
    for (std::size_t i = 0; i < analogCount; ++i) {
      double raw = 0.0;
      if (analogFloat) {
        raw = readLittleEndian<float>(bytes, cursor);
      } else if (analogBytes == 4) {
        raw = static_cast<double>(readLittleEndian<std::int32_t>(bytes, cursor));
      } else {
        raw = static_cast<double>(readLittleEndian<std::int16_t>(bytes, cursor));
      }
      sample.analog.push_back(scaleAnalog(record.analogChannels[i], raw));
      cursor += static_cast<std::size_t>(analogBytes);
    }

    sample.digital.reserve(digitalCount);
    for (std::size_t i = 0; i < digitalCount; ++i) {
      sample.digital.push_back(digitalBit(bytes, cursor, i));
    }
    record.samples.push_back(std::move(sample));
  }
}

} // namespace comtrade
