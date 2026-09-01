/**
 * @file grab.cpp
 * @brief Grabs a single frame from a Mightex TCE-1304-U camera and prints
 * basic statistics (and, optionally, the frame data) to stdout.
 */
#include "cli_common.hpp"
#include "defines.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <cxxopts.hpp>
#include <iostream>
#include <memory>
#include <mightex/mightex.hpp>
#include <stdexcept>
#include <string>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

namespace {

/// Set by the SIGINT handler so a --loop run can break out and let Camera's
/// destructor release the device cleanly, instead of dying mid-transfer.
volatile std::sig_atomic_t g_stop_requested = 0;
void handle_sigint(int) { g_stop_requested = 1; }

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

using mightex_cli::parse_exposure_ms;

/// Parses a "LINESxCOLS" plot size, e.g. "10x80".
std::pair<int, int> parse_plot_dims(const std::string &s) {
  auto x = s.find_first_of("xX");
  std::string lines_str = x == std::string::npos ? std::string() : s.substr(0, x);
  std::string cols_str = x == std::string::npos ? std::string() : s.substr(x + 1);
  char *end_l = nullptr, *end_c = nullptr;
  long lines = std::strtol(lines_str.c_str(), &end_l, 10);
  long cols = std::strtol(cols_str.c_str(), &end_c, 10);
  bool l_ok =
      !lines_str.empty() && end_l == lines_str.c_str() + lines_str.size();
  bool c_ok = !cols_str.empty() && end_c == cols_str.c_str() + cols_str.size();
  if (x == std::string::npos || !l_ok || !c_ok || lines <= 0 || cols <= 0)
    throw std::invalid_argument("invalid plot size '" + s +
                                 "', expected LINESxCOLS (e.g. 10x80)");
  return {static_cast<int>(lines), static_cast<int>(cols)};
}

/// Appends the UTF-8 encoding of a Braille Patterns codepoint (always in the
/// U+2800-U+28FF range, i.e. always 3 UTF-8 bytes).
void append_braille_utf8(std::string &out, unsigned dot_bits) {
  unsigned cp = 0x2800u + dot_bits;
  out += static_cast<char>(0xE0 | ((cp >> 12) & 0x0F));
  out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
  out += static_cast<char>(0x80 | (cp & 0x3F));
}

/// Renders `raw` as a terminal bar chart: pixel index on the x-axis,
/// intensity on the y-axis, using 2 (horizontal) x 3 (vertical) braille dots
/// per character cell -- i.e. each bar is filled solid (both dot columns lit
/// together) from zero up to that column's (max-downsampled) intensity.
/// Dots at or below dark_mean print red; above it, green, except columns
/// with at least one saturating pixel (>= Mightex::MaxPixelValue), which
/// print yellow instead. The y-axis auto-scales to the frame's own max
/// unless `fullscale` is set, in which case it always spans
/// 0..Mightex::MaxPixelValue (values above that are simply clipped to the
/// top row).
void render_plot(std::span<const uint16_t> raw, uint16_t dark_mean, int lines,
                  int cols, bool fullscale) {
  const int total_rows = lines * 3;
  const uint16_t y_min = 0;
  uint16_t y_max = fullscale ? Mightex::MaxPixelValue
                              : *std::max_element(raw.begin(), raw.end());
  if (y_max <= dark_mean)
    y_max = static_cast<uint16_t>(dark_mean + 1);
  double span = static_cast<double>(y_max) - y_min;

  // One representative (max-downsampled) height per column, in dot-rows
  // filled from the bottom (0..total_rows), plus whether any pixel in that
  // column's bucket reaches the sensor's saturation ceiling.
  std::vector<int> filled(cols);
  std::vector<bool> saturating(cols);
  for (int c = 0; c < cols; c++) {
    auto start = static_cast<std::size_t>(c) * raw.size() / cols;
    auto end = static_cast<std::size_t>(c + 1) * raw.size() / cols;
    end = std::clamp(end, start + 1, raw.size());
    uint16_t v = raw[start];
    bool sat = false;
    for (std::size_t i = start; i < end; i++) {
      v = std::max(v, raw[i]);
      sat = sat || raw[i] >= Mightex::MaxPixelValue;
    }
    int f = static_cast<int>((static_cast<double>(v) - y_min) / span *
                              total_rows); // rounded down
    filled[c] = std::clamp(f, 0, total_rows);
    saturating[c] = sat;
  }

  int dark_row = static_cast<int>(
      (static_cast<double>(dark_mean) - y_min) / span * total_rows);
  dark_row = std::clamp(dark_row, 0, total_rows - 1);

  constexpr const char *Red = "\033[31m";
  constexpr const char *Green = "\033[32m";
  constexpr const char *Yellow = "\033[33m";
  constexpr const char *Reset = "\033[0m";
  // Both braille dot-columns are lit together for a given sub-row, for a
  // solid-looking bar rather than a single-dot-wide line.
  constexpr unsigned SubRowBits[3] = {0x01u | 0x08u, 0x02u | 0x10u,
                                       0x04u | 0x20u}; // top, mid, bottom

  for (int r = 0; r < lines; r++) {
    int g_bottom = (lines - 1 - r) * 3;
    int g_top = g_bottom + 2;
    bool row_is_red = g_top < dark_row;

    std::string line;
    line.reserve(static_cast<std::size_t>(cols) * 8);
    const char *cur_color = nullptr; // color currently "open" in the stream
    bool any_filled = false;
    for (int c = 0; c < cols; c++) {
      unsigned bits = 0;
      for (int j = 0; j < 3; j++) { // j: 0=top sub-row .. 2=bottom sub-row
        int g = g_bottom + (2 - j);
        if (g < filled[c])
          bits |= SubRowBits[j];
      }
      if (bits == 0) {
        line += ' ';
        continue;
      }
      any_filled = true;
      const char *color =
          row_is_red ? Red : (saturating[c] ? Yellow : Green);
      if (color != cur_color) {
        line += color;
        cur_color = color;
      }
      append_braille_utf8(line, bits);
    }
    if (any_filled)
      line += Reset;
    std::cout << line << "\n";
  }
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
      ("plot", "plot the raw sensor reading as a terminal chart (2x3-dot "
       "characters, a la btop); optional LINESxCOLS size, default 4x75; "
       "implies -n",
       cxxopts::value<std::string>()->implicit_value("4x75"))
      ("loop", "continuously re-grab and redraw the plot in place, at this "
       "rate in fps (default 25); implies --plot; stop with Ctrl-C",
       cxxopts::value<double>()->implicit_value("25"))
      ("fullscale", "scale the plot's vertical axis to 0..max theoretical "
       "pixel value, instead of auto-scaling to the frame's own max",
       cxxopts::value<bool>()->default_value("false"))
      ("reset", "reset the device (software equivalent of unplug/replug) "
       "before use; can help if pixels get stuck at max value",
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
  bool fullscale = result["fullscale"].as<bool>();
  bool do_plot = result.count("plot") > 0;
  int plot_lines = 4, plot_cols = 75;
  if (do_plot) {
    try {
      std::tie(plot_lines, plot_cols) =
          parse_plot_dims(result["plot"].as<std::string>());
    } catch (const std::exception &e) {
      std::cerr << "Invalid arguments: " << e.what() << "\n";
      return EXIT_FAILURE;
    }
  }

  bool do_loop = result.count("loop") > 0;
  double fps = do_loop ? result["loop"].as<double>() : 0.0;
  if (do_loop && !(fps > 0)) {
    std::cerr << "Invalid arguments: --loop fps must be positive\n";
    return EXIT_FAILURE;
  }
  if (do_loop)
    do_plot = true;
  if (do_plot)
    nodata = true;

  std::unique_ptr<Mightex::Camera> cam;
  try {
    cam = std::make_unique<Mightex::Camera>();
  } catch (const std::exception &e) {
    std::cerr << "No Mightex camera detected or unable to connect: "
              << e.what() << "\n";
    return EXIT_FAILURE;
  }

  if (result["reset"].as<bool>()) {
    try {
      cam->reset_device();
      std::cerr << "Device reset.\n";
    } catch (const std::exception &e) {
      std::cerr << "Failed resetting device: " << e.what() << "\n";
      return EXIT_FAILURE;
    }
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

  if (do_loop)
    std::signal(SIGINT, handle_sigint);

  // Lines occupied by the redrawable block (stats + plot), for --loop.
  const int redraw_lines = 4 + (do_plot ? plot_lines : 0);
  auto next_frame_at = std::chrono::steady_clock::now();
  bool first_frame = true;

  do {
    // wait for a frame to be available
    try {
      while (cam->buffered_frame_count() == 0) {
        if (g_stop_requested)
          break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }
    } catch (const std::exception &e) {
      std::cerr << "Error reading buffer count, exiting: " << e.what()
                << "\n";
      return EXIT_FAILURE;
    }
    if (g_stop_requested)
      break;

    // read the frame
    try {
      cam->read_frame();
    } catch (const std::exception &e) {
      std::cerr << "Failed reading frame, exiting: " << e.what() << "\n";
      return EXIT_FAILURE;
    }
    cam->apply_filter();
    cam->apply_estimator();

    if (do_loop && !first_frame)
      // move the cursor back up over the previous block and erase it
      std::cout << "\033[" << redraw_lines << "A\033[0J";
    first_frame = false;

    std::cerr << "Dark current level: " << cam->dark_mean() << "\n";
    std::cerr << "Mean value: " << stats.avg << "\n";
    std::cerr << "Std.dev.: " << stats.std << "\n";
    std::cerr << "Range: " << stats.min << " - " << stats.max << "\n";

    // plot or print frame data
    if (do_plot) {
      render_plot(cam->raw_frame(), cam->dark_mean(), plot_lines, plot_cols,
                  fullscale);
    } else if (!nodata) {
      auto raw_data = cam->raw_frame();
      auto data = cam->frame();
      for (std::size_t i = 0; i < cam->pixel_count(); i++)
        std::cout << i << " " << raw_data[i] << " " << data[i] << "\n";
    }
    std::cout << std::flush;

    if (do_loop) {
      next_frame_at += std::chrono::duration_cast<
          std::chrono::steady_clock::duration>(
          std::chrono::duration<double>(1.0 / fps));
      auto now = std::chrono::steady_clock::now();
      if (next_frame_at > now)
        std::this_thread::sleep_for(next_frame_at - now);
      else
        next_frame_at = now; // fell behind: don't accumulate drift
    }
  } while (do_loop && !g_stop_requested);

  return EXIT_SUCCESS;
}
