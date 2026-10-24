// JSON's small pieces, for the reports and the registry the level editors read (ReportJson.h, Hazards.h)
#pragma once

#include <string>

namespace cs {

std::string jsonNum(double v);                // %.4g
std::string jsonEsc(const std::string& s);    // a JSON string's insides

} // namespace cs
