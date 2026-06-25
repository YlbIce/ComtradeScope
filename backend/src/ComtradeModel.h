#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace comtrade {

using Json = nlohmann::json;

enum class ChannelKind {
  Analog,
  Digital
};

struct AnalogChannel {
  int index = 0;
  std::string id;
  std::string phase;
  std::string circuit;
  std::string unit;
  double multiplier = 1.0;
  double adder = 0.0;
  double skew = 0.0;
  double minRaw = 0.0;
  double maxRaw = 0.0;
  double primary = 1.0;
  double secondary = 1.0;
  std::string scaling = "P";
};

struct DigitalChannel {
  int index = 0;
  std::string id;
  std::string phase;
  std::string circuit;
  int normalState = 0;
};

struct SampleRateBlock {
  double rate = 0.0;
  std::int64_t endSample = 0;
};

struct Sample {
  std::int64_t index = 0;
  double timeSeconds = 0.0;
  std::vector<double> analog;
  std::vector<int> digital;
};

struct ComtradeRecord {
  std::string cfgPath;
  std::string datPath;
  std::string stationName;
  std::string deviceId;
  std::string revisionYear;
  std::string frequency;
  std::string startTime;
  std::string triggerTime;
  std::string dataFileType;
  std::string timestampMultiplier = "1";
  std::vector<AnalogChannel> analogChannels;
  std::vector<DigitalChannel> digitalChannels;
  std::vector<SampleRateBlock> sampleRates;
  std::vector<Sample> samples;
  std::vector<std::string> warnings;
};

Json toJson(const AnalogChannel& channel);
Json toJson(const DigitalChannel& channel);
Json toJson(const SampleRateBlock& block);

} // namespace comtrade
