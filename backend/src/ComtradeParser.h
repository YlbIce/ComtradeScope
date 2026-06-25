#pragma once

#include "ComtradeModel.h"

#include <filesystem>
#include <string>

namespace comtrade {

class ComtradeParser final {
public:
  ComtradeRecord load(const std::filesystem::path& cfgPath, const std::filesystem::path& datPath = {});

private:
  void parseCfg(ComtradeRecord& record, const std::filesystem::path& cfgPath);
  void parseDat(ComtradeRecord& record, const std::filesystem::path& datPath);
  void parseAsciiDat(ComtradeRecord& record, const std::filesystem::path& datPath);
  void parseBinaryDat(ComtradeRecord& record, const std::filesystem::path& datPath, int analogBytes, bool analogFloat);
};

} // namespace comtrade
