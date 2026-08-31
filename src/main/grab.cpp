/**
 * @file grab.cpp
 * @brief Grabs a single frame from a Mightex TCE-1304-U camera and prints
 * basic statistics (and, optionally, the frame data) to stdout.
 */
#include "defines.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cxxopts.hpp>
#include <iostream>
#include <memory>
#include <mightex/mightex.hpp>
#include <stdexcept>
#include <thread>

namespace {

struct Stats {
  double avg = 0, std = 0;
  uint16_t min = 0, max = 0;
};

double mean(std::span<const uint16_t> d) {
  uint32_t sum = 0;
  for (uint16_t v : d)
    sum += v;
  return static_cast<double>(sum) / d.size();
}

double stdev(std::span<const uint16_t> data, Stats &s) {
  s.avg = mean(data);
  s.min = data[0];
  s.max = data[0];
  for (uint16_t v : data) {
    s.min = std::min(s.min, v);
    s.max = std::max(s.max, v);
    s.std += std::pow(v - s.avg, 2);
  }
  s.std = std::sqrt(s.std / (data.size() - 1));
  return s.std;
}

/// Parses an exposure time given either as a plain value in milliseconds
/// (e.g. "5", "0.1") or as a photographic shutter fraction of a second
/// (e.g. "1/125", meaning 1000/125 = 8 ms).
float parse_exposure_ms(const std::string &s) {
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

} // namespace

int main(int argc, char *argv[]) {
  cxxopts::Options options(argv[0], std::string("Mightex1304 v.") +
                                         GIT_COMMIT_HASH + " for " +
                                         CMAKE_PLATFORM + ", " +
                                         CMAKE_BUILD_TYPE + " build.");
  // clang-format off
  options.add_options()
      ("e,exp", "exposure time: milliseconds (min: 0.1), or a photographic "
       "shutter fraction of a second, e.g. 1/125",
       cxxopts::value<std::string>()->default_value("0.1"))
      ("n,nodata", "print no data",
       cxxopts::value<bool>()->default_value("false"))
      ("r,raw", "apply analysis to raw values",
       cxxopts::value<bool>()->default_value("false"))
      ("h,help", "print usage");
  // clang-format on

  cxxopts::ParseResult result;
  try {
    result = options.parse(argc, argv);
  } catch (const cxxopts::exceptions::exception &e) {
    std::cerr << "Invalid arguments: " << e.what() << "\n";
    return EXIT_FAILURE;
  }

  if (result.count("help")) {
    std::cout << options.help() << std::endl;
    return EXIT_SUCCESS;
  }

  float exposure;
  try {
    exposure = parse_exposure_ms(result["exp"].as<std::string>());
  } catch (const std::exception &e) {
    std::cerr << "Invalid arguments: " << e.what() << "\n";
    return EXIT_FAILURE;
  }
  bool nodata = result["nodata"].as<bool>();
  bool nofilter = result["raw"].as<bool>();

  std::unique_ptr<Mightex::Camera> cam;
  try {
    cam = std::make_unique<Mightex::Camera>();
  } catch (const std::exception &e) {
    std::cerr << "No Mightex camera detected or unable to connect: "
              << e.what() << "\n";
    return EXIT_FAILURE;
  }

  Stats stats;
  // custom estimator for calculating standard deviation of dark scene
  cam->set_estimator(
      [&stats](std::span<const uint16_t> data) { return stdev(data, stats); });

  if (nofilter)
    cam->set_filter({});

  try {
    cam->set_exposure_time(exposure);
  } catch (const std::exception &e) {
    std::cerr << "Failed setting esposure time: " << e.what() << "\n";
  }
  std::cerr << "Esposure time set to " << exposure << " ms\n";

  try {
    cam->set_mode(Mightex::Mode::Normal);
  } catch (const std::exception &e) {
    std::cerr << "Failed setting mode: " << e.what() << "\n";
  }

  // wait for a frame to be available
  try {
    while (cam->buffered_frame_count() == 0)
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
  } catch (const std::exception &e) {
    std::cerr << "Error reading buffer count, exiting: " << e.what() << "\n";
    return EXIT_FAILURE;
  }

  // read the frame
  try {
    cam->read_frame();
  } catch (const std::exception &e) {
    std::cerr << "Failed reading frame, exiting: " << e.what() << "\n";
    return EXIT_FAILURE;
  }
  cam->apply_filter();
  cam->apply_estimator();

  std::cerr << "Dark current level: " << cam->dark_mean() << "\n";
  std::cerr << "Mean value: " << stats.avg << "\n";
  std::cerr << "Std.dev.: " << stats.std << "\n";
  std::cerr << "Range: " << stats.min << " - " << stats.max << "\n";

  // print frame data
  if (!nodata) {
    auto raw_data = cam->raw_frame();
    auto data = cam->frame();
    for (std::size_t i = 0; i < cam->pixel_count(); i++)
      std::cout << i << " " << raw_data[i] << " " << data[i] << "\n";
  }

  return EXIT_SUCCESS;
}
