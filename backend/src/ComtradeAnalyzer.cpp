#include "ComtradeAnalyzer.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <limits>
#include <map>

namespace comtrade {

namespace {

constexpr double kPi = 3.141592653589793238462643383279502884;

double finiteOrZero(double value) {
  return std::isfinite(value) ? value : 0.0;
}

double recordDuration(const ComtradeRecord& record) {
  if (record.samples.size() < 2) {
    return 0.0;
  }
  return std::max(0.0, record.samples.back().timeSeconds - record.samples.front().timeSeconds);
}

double inferSampleRate(const ComtradeRecord& record) {
  if (!record.sampleRates.empty() && record.sampleRates.front().rate > 0.0) {
    return record.sampleRates.front().rate;
  }
  const double duration = recordDuration(record);
  if (duration <= 0.0 || record.samples.size() < 2) {
    return 0.0;
  }
  return static_cast<double>(record.samples.size() - 1) / duration;
}

double rms(const std::vector<double>& values) {
  if (values.empty()) {
    return 0.0;
  }
  long double sum = 0.0;
  for (double value : values) {
    sum += static_cast<long double>(value) * value;
  }
  return std::sqrt(static_cast<double>(sum / values.size()));
}

std::complex<double> dftComponent(const std::vector<double>& values, int harmonic) {
  if (values.empty()) {
    return {};
  }

  std::complex<double> sum {};
  const double count = static_cast<double>(values.size());
  for (std::size_t i = 0; i < values.size(); ++i) {
    const double angle = -2.0 * kPi * static_cast<double>(harmonic) * static_cast<double>(i) / count;
    sum += std::polar(values[i], angle);
  }
  // 正弦量基波幅值近似为 2/N * DFT，RMS 为峰值 / sqrt(2)。
  return (2.0 / count) * sum / std::sqrt(2.0);
}

double angleDegrees(const std::complex<double>& value) {
  return std::atan2(value.imag(), value.real()) * 180.0 / kPi;
}

std::vector<double> channelValues(const ComtradeRecord& record, std::size_t channelIndex) {
  std::vector<double> values;
  values.reserve(record.samples.size());
  for (const auto& sample : record.samples) {
    if (channelIndex < sample.analog.size()) {
      values.push_back(sample.analog[channelIndex]);
    }
  }
  return values;
}

std::vector<double> lastCycleValues(const ComtradeRecord& record, std::size_t channelIndex) {
  const double rate = inferSampleRate(record);
  double frequency = 50.0;
  try {
    if (!record.frequency.empty()) {
      frequency = std::stod(record.frequency);
    }
  } catch (...) {
    frequency = 50.0;
  }

  const std::size_t cycleSamples = rate > 0.0 && frequency > 0.0
    ? static_cast<std::size_t>(std::round(rate / frequency))
    : 0;
  auto values = channelValues(record, channelIndex);
  if (cycleSamples == 0 || values.size() <= cycleSamples) {
    return values;
  }
  return {values.end() - static_cast<std::ptrdiff_t>(cycleSamples), values.end()};
}

std::string phaseKey(const AnalogChannel& channel) {
  std::string value = channel.phase;
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
    return static_cast<char>(std::toupper(ch));
  });
  return value;
}

Json complexToJson(const std::complex<double>& value) {
  return {
    {"real", value.real()},
    {"imag", value.imag()},
    {"magnitude", std::abs(value)},
    {"angle", angleDegrees(value)}
  };
}

} // namespace

Json ComtradeAnalyzer::buildLoadResult(const ComtradeRecord& record, std::size_t maxPoints) const {
  const Json phasors = buildPhasors(record);
  return {
    {"summary", buildSummary(record)},
    {"waveform", buildWaveform(record, maxPoints)},
    {"analogStats", buildAnalogStats(record)},
    {"digitalEvents", buildDigitalEvents(record)},
    {"phasors", phasors},
    {"sequence", buildSequenceComponents(phasors)},
    {"warnings", record.warnings}
  };
}

Json ComtradeAnalyzer::buildSummary(const ComtradeRecord& record) const {
  Json rates = Json::array();
  for (const auto& block : record.sampleRates) {
    rates.push_back(toJson(block));
  }

  return {
    {"cfgPath", record.cfgPath},
    {"datPath", record.datPath},
    {"stationName", record.stationName},
    {"deviceId", record.deviceId},
    {"revisionYear", record.revisionYear},
    {"frequency", record.frequency},
    {"startTime", record.startTime},
    {"triggerTime", record.triggerTime},
    {"dataFileType", record.dataFileType},
    {"timestampMultiplier", record.timestampMultiplier},
    {"analogCount", record.analogChannels.size()},
    {"digitalCount", record.digitalChannels.size()},
    {"sampleCount", record.samples.size()},
    {"duration", recordDuration(record)},
    {"sampleRate", inferSampleRate(record)},
    {"sampleRates", rates}
  };
}

Json ComtradeAnalyzer::buildWaveform(const ComtradeRecord& record, std::size_t maxPoints) const {
  Json analogChannels = Json::array();
  for (const auto& channel : record.analogChannels) {
    analogChannels.push_back(toJson(channel));
  }
  Json digitalChannels = Json::array();
  for (const auto& channel : record.digitalChannels) {
    digitalChannels.push_back(toJson(channel));
  }

  const std::size_t stride = std::max<std::size_t>(1, record.samples.size() / std::max<std::size_t>(1, maxPoints));
  Json samples = Json::array();
  for (std::size_t i = 0; i < record.samples.size(); i += stride) {
    const auto& sample = record.samples[i];
    samples.push_back({
      {"index", sample.index},
      {"time", sample.timeSeconds},
      {"analog", sample.analog},
      {"digital", sample.digital}
    });
  }

  return {
    {"stride", stride},
    {"samples", samples},
    {"analogChannels", analogChannels},
    {"digitalChannels", digitalChannels}
  };
}

Json ComtradeAnalyzer::buildAnalogStats(const ComtradeRecord& record) const {
  Json result = Json::array();
  for (std::size_t channel = 0; channel < record.analogChannels.size(); ++channel) {
    double minValue = std::numeric_limits<double>::infinity();
    double maxValue = -std::numeric_limits<double>::infinity();
    double sum = 0.0;
    std::vector<double> values;
    values.reserve(record.samples.size());

    for (const auto& sample : record.samples) {
      if (channel >= sample.analog.size()) {
        continue;
      }
      const double value = sample.analog[channel];
      minValue = std::min(minValue, value);
      maxValue = std::max(maxValue, value);
      sum += value;
      values.push_back(value);
    }

    const double mean = values.empty() ? 0.0 : sum / static_cast<double>(values.size());
    const auto fundamental = dftComponent(lastCycleValues(record, channel), 1);
    const auto second = dftComponent(lastCycleValues(record, channel), 2);
    const auto third = dftComponent(lastCycleValues(record, channel), 3);
    const double fundamentalRms = std::abs(fundamental);
    const double thd = fundamentalRms > 1e-12
      ? std::sqrt(std::norm(second) + std::norm(third)) / fundamentalRms
      : 0.0;

    result.push_back({
      {"index", record.analogChannels[channel].index},
      {"id", record.analogChannels[channel].id},
      {"unit", record.analogChannels[channel].unit},
      {"min", finiteOrZero(minValue)},
      {"max", finiteOrZero(maxValue)},
      {"mean", mean},
      {"rms", rms(values)},
      {"fundamentalRms", fundamentalRms},
      {"phaseAngle", angleDegrees(fundamental)},
      {"thd23", thd}
    });
  }
  return result;
}

Json ComtradeAnalyzer::buildDigitalEvents(const ComtradeRecord& record) const {
  Json result = Json::array();
  if (record.samples.empty()) {
    return result;
  }

  for (std::size_t channel = 0; channel < record.digitalChannels.size(); ++channel) {
    int last = channel < record.samples.front().digital.size() ? record.samples.front().digital[channel] : 0;
    for (std::size_t i = 1; i < record.samples.size(); ++i) {
      const int current = channel < record.samples[i].digital.size() ? record.samples[i].digital[channel] : 0;
      if (current != last) {
        result.push_back({
          {"channelIndex", record.digitalChannels[channel].index},
          {"channelId", record.digitalChannels[channel].id},
          {"time", record.samples[i].timeSeconds},
          {"from", last},
          {"to", current},
          {"edge", current ? "rising" : "falling"}
        });
        last = current;
      }
    }
  }
  return result;
}

Json ComtradeAnalyzer::buildPhasors(const ComtradeRecord& record) const {
  Json result = Json::array();
  for (std::size_t channel = 0; channel < record.analogChannels.size(); ++channel) {
    const auto fundamental = dftComponent(lastCycleValues(record, channel), 1);
    result.push_back({
      {"channelIndex", record.analogChannels[channel].index},
      {"channelId", record.analogChannels[channel].id},
      {"phase", record.analogChannels[channel].phase},
      {"unit", record.analogChannels[channel].unit},
      {"real", fundamental.real()},
      {"imag", fundamental.imag()},
      {"magnitude", std::abs(fundamental)},
      {"angle", angleDegrees(fundamental)}
    });
  }
  return result;
}

Json ComtradeAnalyzer::buildSequenceComponents(const Json& phasors) const {
  std::map<std::string, std::complex<double>> phaseValues;
  for (const auto& item : phasors) {
    const std::string phase = item.value("phase", "");
    if (phase.empty()) {
      continue;
    }
    std::string key = phase;
    std::transform(key.begin(), key.end(), key.begin(), [](unsigned char ch) {
      return static_cast<char>(std::toupper(ch));
    });
    if (key == "A" || key == "B" || key == "C") {
      phaseValues[key] = {
        item.value("real", 0.0),
        item.value("imag", 0.0)
      };
    }
  }

  if (!phaseValues.contains("A") || !phaseValues.contains("B") || !phaseValues.contains("C")) {
    return {
      {"available", false},
      {"reason", "需要至少 A/B/C 三相模拟通道才能计算序分量"}
    };
  }

  const auto a = std::polar(1.0, 2.0 * kPi / 3.0);
  const auto va = phaseValues["A"];
  const auto vb = phaseValues["B"];
  const auto vc = phaseValues["C"];
  const auto zero = (va + vb + vc) / 3.0;
  const auto positive = (va + a * vb + a * a * vc) / 3.0;
  const auto negative = (va + a * a * vb + a * vc) / 3.0;

  return {
    {"available", true},
    {"zero", complexToJson(zero)},
    {"positive", complexToJson(positive)},
    {"negative", complexToJson(negative)},
    {"unbalance", std::abs(positive) > 1e-12 ? std::abs(negative) / std::abs(positive) : 0.0}
  };
}

} // namespace comtrade
