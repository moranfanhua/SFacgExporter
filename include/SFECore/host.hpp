#pragma once
#include "SFECore/json.hpp"
#include <string>

namespace sf {
Json host_call(const Json &request);
std::string base64_encode(const std::string &bytes);
std::string base64_decode(const std::string &text);
} // namespace sf
