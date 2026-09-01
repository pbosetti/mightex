/**
 * @file cli_common.hpp
 * @brief Small CLI-argument parsing helpers shared by grab.cpp and
 * photo.cpp.
 */
#pragma once

#include <cctype>
#include <cstdlib>
#include <stdexcept>
#include <string>

namespace mightex_cli {

/// Parses an exposure time given either as a plain value in milliseconds
/// (e.g. "5", "0.1") or as a photographic shutter fraction of a second
/// (e.g. "1/125", meaning 1000/125 = 8 ms).
inline float parse_exposure_ms(const std::string &s) {
  auto slash = s.find('/');
  if (slash != std::string::npos) {
    std::string num_str = s.substr(0, slash);
    std::string den_str = s.substr(slash + 1);
    char *end_num = nullptr, *end_den = nullptr;
    double num = std::strtod(num_str.c_str(), &end_num);
    double den = std::strtod(den_str.c_str(), &end_den);
    bool num_ok =
        !num_str.empty() && end_num == num_str.c_str() + num_str.size();
    bool den_ok =
        !den_str.empty() && end_den == den_str.c_str() + den_str.size();
    if (!num_ok || !den_ok || den == 0)
      throw std::invalid_argument("invalid exposure fraction '" + s + "'");
    return static_cast<float>((num / den) * 1000.0); // seconds -> ms
  }
  char *end = nullptr;
  float ms = std::strtof(s.c_str(), &end);
  if (s.empty() || end != s.c_str() + s.size())
    throw std::invalid_argument("invalid exposure value '" + s + "'");
  return ms;
}

/// Parses a byte-size string such as "100Mb", "500k", "2G", or a bare
/// number of bytes. Units are binary (1k = 1024), case-insensitive, and the
/// trailing "b" is optional (e.g. "100M" == "100Mb").
inline std::size_t parse_size_bytes(const std::string &s) {
  std::size_t i = 0;
  while (i < s.size() &&
         (std::isdigit(static_cast<unsigned char>(s[i])) || s[i] == '.'))
    i++;
  std::string num_str = s.substr(0, i);
  std::string unit = s.substr(i);
  for (char &c : unit)
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

  char *end = nullptr;
  double num = std::strtod(num_str.c_str(), &end);
  if (num_str.empty() || end != num_str.c_str() + num_str.size() || num < 0)
    throw std::invalid_argument("invalid size '" + s + "'");

  double mult;
  if (unit.empty() || unit == "b")
    mult = 1.0;
  else if (unit == "k" || unit == "kb")
    mult = 1024.0;
  else if (unit == "m" || unit == "mb")
    mult = 1024.0 * 1024.0;
  else if (unit == "g" || unit == "gb")
    mult = 1024.0 * 1024.0 * 1024.0;
  else
    throw std::invalid_argument("invalid size unit in '" + s + "'");
  return static_cast<std::size_t>(num * mult);
}

/// Parses a duration in seconds, given as a plain number or with a trailing
/// "s" (e.g. "10", "10s", "2.5s").
inline double parse_duration_s(const std::string &s) {
  std::string t = s;
  if (!t.empty() && (t.back() == 's' || t.back() == 'S'))
    t.pop_back();
  char *end = nullptr;
  double v = std::strtod(t.c_str(), &end);
  if (t.empty() || end != t.c_str() + t.size() || v <= 0)
    throw std::invalid_argument("invalid duration '" + s + "'");
  return v;
}

} // namespace mightex_cli
