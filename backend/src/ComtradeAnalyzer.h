#pragma once

#include "ComtradeModel.h"

#include <cstddef>

namespace comtrade {

class ComtradeAnalyzer final {
public:
  Json buildLoadResult(const ComtradeRecord& record, std::size_t maxPoints = 5000) const;

private:
  Json buildSummary(const ComtradeRecord& record) const;
  Json buildWaveform(const ComtradeRecord& record, std::size_t maxPoints) const;
  Json buildAnalogStats(const ComtradeRecord& record) const;
  Json buildDigitalEvents(const ComtradeRecord& record) const;
  Json buildPhasors(const ComtradeRecord& record) const;
  Json buildSequenceComponents(const Json& phasors) const;
};

} // namespace comtrade
