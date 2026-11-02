#pragma once

#if defined(__has_include) &&__has_include(<format>)
#include <format>
#define FAKE_FORMAT_IS_REAL
#else
namespace std {
template <typename... Args>
std::string format(const std::string_view fmt, Args &&...) {
  return std::string{fmt};
}
}
#endif
