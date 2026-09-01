/**
 * @file photo.cpp
 * @brief Assembles a 2D image from a Mightex TCE-1304-U camera by grabbing
 * successive pixel vectors (frames) and stacking them side by side, one
 * frame per image column.
 */
#include "cli_common.hpp"
#include "defines.h"

#include <lodepng.h>

#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <cxxopts.hpp>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mightex/mightex.hpp>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

volatile std::sig_atomic_t g_stop_requested = 0;
void handle_sigint(int) { g_stop_requested = 1; }

enum class Format { Raw, Pgm, Png, Dng };

Format parse_format(const std::string &s) {
  std::string f = s;
  for (char &c : f)
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  if (f == "raw")
    return Format::Raw;
  if (f == "pgm")
    return Format::Pgm;
  if (f == "png")
    return Format::Png;
  if (f == "dng")
    return Format::Dng;
  throw std::invalid_argument("invalid --format '" + s +
                               "', expected raw, pgm, png or dng");
}

const char *format_extension(Format fmt) {
  switch (fmt) {
  case Format::Raw:
    return ".mxr";
  case Format::Pgm:
    return ".pgm";
  case Format::Png:
    return ".png";
  case Format::Dng:
    return ".dng";
  }
  return "";
}

std::string zero_pad(int n, int width) {
  std::ostringstream oss;
  oss << std::setw(width) << std::setfill('0') << n;
  return oss.str();
}

/// Lays out captured columns (one per frame, in capture order) into a
/// row-major width*height matrix, native uint16_t byte order. `reverse`
/// (--dir=RL) piles new frames onto the left instead of the right.
std::vector<uint16_t>
assemble_matrix(const std::vector<std::vector<uint16_t>> &columns,
                 bool reverse) {
  std::size_t width = columns.size();
  std::size_t height = Mightex::Pixels;
  std::vector<uint16_t> out(width * height);
  for (std::size_t c = 0; c < width; c++) {
    const auto &col = columns[reverse ? (width - 1 - c) : c];
    for (std::size_t r = 0; r < height; r++)
      out[r * width + c] = col[r];
  }
  return out;
}

/// Per the PGM and PNG specs, 16-bit samples are big-endian regardless of
/// host byte order.
std::vector<uint8_t> to_big_endian_bytes(const std::vector<uint16_t> &s) {
  std::vector<uint8_t> out(s.size() * 2);
  for (std::size_t i = 0; i < s.size(); i++) {
    out[2 * i] = static_cast<uint8_t>(s[i] >> 8);
    out[2 * i + 1] = static_cast<uint8_t>(s[i] & 0xFF);
  }
  return out;
}

void save_raw(const std::string &path, const std::vector<uint16_t> &data,
              uint32_t width, uint32_t height) {
  std::ofstream f(path, std::ios::binary);
  if (!f)
    throw std::runtime_error("cannot open '" + path + "' for writing");
  const char magic[4] = {'M', 'X', 'R', '1'};
  f.write(magic, sizeof(magic));
  f.write(reinterpret_cast<const char *>(&width), sizeof(width));
  f.write(reinterpret_cast<const char *>(&height), sizeof(height));
  f.write(reinterpret_cast<const char *>(data.data()),
          static_cast<std::streamsize>(data.size() * sizeof(uint16_t)));
  if (!f)
    throw std::runtime_error("write failed for '" + path + "'");
}

void save_pgm(const std::string &path, const std::vector<uint16_t> &data,
              uint32_t width, uint32_t height) {
  std::ofstream f(path, std::ios::binary);
  if (!f)
    throw std::runtime_error("cannot open '" + path + "' for writing");
  f << "P5\n" << width << " " << height << "\n65535\n";
  auto be = to_big_endian_bytes(data);
  f.write(reinterpret_cast<const char *>(be.data()),
          static_cast<std::streamsize>(be.size()));
  if (!f)
    throw std::runtime_error("write failed for '" + path + "'");
}

void save_png(const std::string &path, const std::vector<uint16_t> &data,
              uint32_t width, uint32_t height) {
  auto be = to_big_endian_bytes(data);
  unsigned err =
      lodepng::encode(path, be, width, height, LCT_GREY, 16 /* bitdepth */);
  if (err)
    throw std::runtime_error("PNG encode failed for '" + path +
                              "': " + lodepng_error_text(err));
}

/// Writes a minimal, spec-valid "Linear DNG" (uncompressed, 16-bit,
/// PhotometricInterpretation=LinearRaw): a monochrome sensor has no
/// CFA/color-matrix data to give, so this is a synthetic raw-ish container,
/// not a real camera-sensor raw file. Byte order is little-endian ("II"),
/// matching every desktop platform this project targets, so pixel samples
/// are written as-is (no swap, unlike PGM/PNG).
///
/// Readers like exiftool/libtiff will happily parse a single flat IFD, but
/// Photoshop/Camera Raw's stricter DNG reader won't open one: it expects
/// IFD0 to be a minimal stub carrying just DNGVersion + a SubIFDs pointer
/// (tag 0x014A), with the actual pixel data in a separate raw SubIFD that
/// explicitly declares NewSubfileType=0. This mirrors that two-level
/// structure. All multi-byte tag *values* must additionally start on an
/// even file offset (TIFF word-alignment rule) -- add_extra() pads for it.
void save_dng(const std::string &path, const std::vector<uint16_t> &pixels,
              uint32_t width, uint32_t height, float /*exposure_ms*/) {
  using Bytes = std::vector<uint8_t>;
  auto put16 = [](Bytes &b, uint16_t v) {
    b.push_back(static_cast<uint8_t>(v & 0xFF));
    b.push_back(static_cast<uint8_t>(v >> 8));
  };
  auto put32 = [](Bytes &b, uint32_t v) {
    b.push_back(static_cast<uint8_t>(v & 0xFF));
    b.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    b.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    b.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
  };
  constexpr uint16_t T_BYTE = 1, T_ASCII = 2, T_SHORT = 3, T_LONG = 4;

  // --- IFD0: minimal stub (SubIFDs pointer + DNGVersion only) ---
  constexpr int Ifd0EntryCount = 2;
  constexpr uint32_t Ifd0Offset = 8;
  constexpr uint32_t Ifd0Size = 2 + Ifd0EntryCount * 12 + 4;
  static_assert(Ifd0Size % 2 == 0);

  // --- Raw SubIFD: the actual image, right after IFD0 ---
  constexpr int RawEntryCount = 17;
  constexpr uint32_t RawIfdOffset = Ifd0Offset + Ifd0Size;
  constexpr uint32_t RawIfdSize = 2 + RawEntryCount * 12 + 4;
  static_assert(RawIfdSize % 2 == 0);
  constexpr uint32_t ExtraOffset = RawIfdOffset + RawIfdSize;

  const std::string make = "Mightex";
  const std::string model = "TCE-1304-U";
  const std::string unique = "Mightex TCE-1304-U Line-Scan Composite";

  std::time_t t = std::time(nullptr);
  std::tm tmv{};
#ifdef _WIN32
  localtime_s(&tmv, &t);
#else
  localtime_r(&t, &tmv);
#endif
  char datetime[20];
  std::snprintf(datetime, sizeof(datetime), "%04d:%02d:%02d %02d:%02d:%02d",
                tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday, tmv.tm_hour,
                tmv.tm_min, tmv.tm_sec);

  Bytes extra;
  auto add_extra = [&](const void *data, std::size_t n) -> uint32_t {
    uint32_t off = ExtraOffset + static_cast<uint32_t>(extra.size());
    const auto *p = static_cast<const uint8_t *>(data);
    extra.insert(extra.end(), p, p + n);
    if (extra.size() % 2 != 0)
      extra.push_back(0); // keep the *next* field's offset even
    return off;
  };
  uint32_t off_make = add_extra(make.c_str(), make.size() + 1);
  uint32_t off_model = add_extra(model.c_str(), model.size() + 1);
  uint32_t off_datetime = add_extra(datetime, sizeof(datetime));
  uint32_t off_unique = add_extra(unique.c_str(), unique.size() + 1);

  uint32_t strip_offset = ExtraOffset + static_cast<uint32_t>(extra.size());
  uint64_t strip_bytes64 =
      static_cast<uint64_t>(width) * height * sizeof(uint16_t);
  if (strip_bytes64 > 0xFFFFFFFFull - strip_offset)
    throw std::runtime_error(
        "image too large for a single DNG file (32-bit TIFF offsets); "
        "lower --max_mem");
  uint32_t strip_bytes = static_cast<uint32_t>(strip_bytes64);

  auto make_entry = [&](Bytes &ifd, uint16_t tag, uint16_t type,
                         uint32_t count, uint32_t raw_le) {
    put16(ifd, tag);
    put16(ifd, type);
    put32(ifd, count);
    put32(ifd, raw_le);
  };

  Bytes ifd0;
  make_entry(ifd0, 330, T_LONG, 1, RawIfdOffset);        // SubIFDs
  make_entry(ifd0, 50706, T_BYTE, 4, 0x00000401u);       // DNGVersion 1.4.0.0

  Bytes raw_ifd;
  // Ascending tag order, as required by the TIFF/DNG spec.
  make_entry(raw_ifd, 254, T_LONG, 1, 0);                  // NewSubfileType
  make_entry(raw_ifd, 256, T_LONG, 1, width);              // ImageWidth
  make_entry(raw_ifd, 257, T_LONG, 1, height);             // ImageLength
  make_entry(raw_ifd, 258, T_SHORT, 1, 16);                // BitsPerSample
  make_entry(raw_ifd, 259, T_SHORT, 1, 1);                 // Compression
  make_entry(raw_ifd, 262, T_SHORT, 1, 34892);      // PhotometricInterp. (LinearRaw)
  make_entry(raw_ifd, 271, T_ASCII, static_cast<uint32_t>(make.size() + 1),
             off_make);
  make_entry(raw_ifd, 272, T_ASCII, static_cast<uint32_t>(model.size() + 1),
             off_model);
  make_entry(raw_ifd, 273, T_LONG, 1, strip_offset);       // StripOffsets
  make_entry(raw_ifd, 277, T_SHORT, 1, 1);                 // SamplesPerPixel
  make_entry(raw_ifd, 278, T_LONG, 1, height);             // RowsPerStrip
  make_entry(raw_ifd, 279, T_LONG, 1, strip_bytes);        // StripByteCounts
  make_entry(raw_ifd, 284, T_SHORT, 1, 1);                 // PlanarConfiguration
  make_entry(raw_ifd, 306, T_ASCII, sizeof(datetime), off_datetime);
  make_entry(raw_ifd, 50708, T_ASCII,
             static_cast<uint32_t>(unique.size() + 1),
             off_unique);                                  // UniqueCameraModel
  make_entry(raw_ifd, 50714, T_LONG, 1, 0);                // BlackLevel
  make_entry(raw_ifd, 50717, T_LONG, 1, Mightex::MaxPixelValue); // WhiteLevel

  Bytes file;
  file.push_back('I');
  file.push_back('I');
  put16(file, 42);
  put32(file, Ifd0Offset);

  put16(file, Ifd0EntryCount);
  file.insert(file.end(), ifd0.begin(), ifd0.end());
  put32(file, 0); // no sibling top-level IFDs

  put16(file, RawEntryCount);
  file.insert(file.end(), raw_ifd.begin(), raw_ifd.end());
  put32(file, 0); // no sibling IFDs under the SubIFDs chain

  file.insert(file.end(), extra.begin(), extra.end());

  std::ofstream f(path, std::ios::binary);
  if (!f)
    throw std::runtime_error("cannot open '" + path + "' for writing");
  f.write(reinterpret_cast<const char *>(file.data()),
          static_cast<std::streamsize>(file.size()));
  f.write(reinterpret_cast<const char *>(pixels.data()), strip_bytes);
  if (!f)
    throw std::runtime_error("write failed for '" + path + "'");
}

void save_chunk(const std::string &path, Format fmt,
                 const std::vector<std::vector<uint16_t>> &columns,
                 bool reverse_dir, float exposure_ms) {
  auto matrix = assemble_matrix(columns, reverse_dir);
  auto width = static_cast<uint32_t>(columns.size());
  auto height = static_cast<uint32_t>(Mightex::Pixels);
  switch (fmt) {
  case Format::Raw:
    save_raw(path, matrix, width, height);
    break;
  case Format::Pgm:
    save_pgm(path, matrix, width, height);
    break;
  case Format::Png:
    save_png(path, matrix, width, height);
    break;
  case Format::Dng:
    save_dng(path, matrix, width, height, exposure_ms);
    break;
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
      ("dir", "direction to pile up pixel vectors: LR (left-to-right) or RL",
       cxxopts::value<std::string>()->default_value("LR"))
      ("fps", "capture rate in frames per second",
       cxxopts::value<double>()->default_value("25"))
      ("e,exp", "exposure time per frame: milliseconds (min: 0.1), or a "
       "photographic shutter fraction of a second, e.g. 1/125",
       cxxopts::value<std::string>()->default_value("0.1"))
      ("frames", "number of frames to acquire (default: continuous, stop "
       "with Ctrl-C)",
       cxxopts::value<long>())
      ("duration", "capture duration in seconds (e.g. 10, 10s); overrides "
       "--frames if both are given",
       cxxopts::value<std::string>())
      ("max_mem", "save, flush the in-memory image and start a new one once "
       "it exceeds this size; e.g. 100Mb, 500k (default unit: bytes)",
       cxxopts::value<std::string>()->implicit_value("100Mb"))
      ("format", "output format: raw, pgm, png, dng",
       cxxopts::value<std::string>()->default_value("png"))
      ("o,out", "output file basename",
       cxxopts::value<std::string>()->default_value("photo"))
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

  float exposure_ms;
  bool reverse_dir;
  double fps;
  bool do_frames = result.count("frames") > 0;
  long frames_target = 0;
  bool do_duration = result.count("duration") > 0;
  double duration_s = 0.0;
  bool do_max_mem = result.count("max_mem") > 0;
  std::size_t max_mem_bytes = 0;
  Format format;
  try {
    exposure_ms = mightex_cli::parse_exposure_ms(result["exp"].as<std::string>());

    std::string dir = result["dir"].as<std::string>();
    for (char &c : dir)
      c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    if (dir != "LR" && dir != "RL")
      throw std::invalid_argument("invalid --dir '" +
                                   result["dir"].as<std::string>() +
                                   "', expected LR or RL");
    reverse_dir = (dir == "RL");

    fps = result["fps"].as<double>();
    if (!(fps > 0))
      throw std::invalid_argument("--fps must be positive");

    if (do_frames) {
      frames_target = result["frames"].as<long>();
      if (frames_target <= 0)
        throw std::invalid_argument("--frames must be positive");
    }
    if (do_duration)
      duration_s = mightex_cli::parse_duration_s(result["duration"].as<std::string>());
    if (do_max_mem)
      max_mem_bytes =
          mightex_cli::parse_size_bytes(result["max_mem"].as<std::string>());

    format = parse_format(result["format"].as<std::string>());
  } catch (const std::exception &e) {
    std::cerr << "Invalid arguments: " << e.what() << "\n";
    return EXIT_FAILURE;
  }

  if (do_frames && do_duration) {
    std::cerr << "Warning: both --frames and --duration given; --duration "
                 "takes precedence.\n";
    do_frames = false;
  }

  std::string out_prefix = result["out"].as<std::string>();
  const char *ext = format_extension(format);

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

  try {
    cam->set_exposure_time(exposure_ms);
  } catch (const std::exception &e) {
    std::cerr << "Failed setting exposure time: " << e.what() << "\n";
  }
  std::cerr << "Exposure time set to " << exposure_ms << " ms\n";

  try {
    cam->set_mode(Mightex::Mode::Normal);
  } catch (const std::exception &e) {
    std::cerr << "Failed setting mode: " << e.what() << "\n";
  }

  std::signal(SIGINT, handle_sigint);

  std::vector<std::vector<uint16_t>> columns;
  const std::size_t bytes_per_frame = Mightex::Pixels * sizeof(uint16_t);
  long frames_captured_total = 0;
  int chunk_index = 1;

  auto flush_chunk = [&]() {
    if (columns.empty())
      return;
    std::string path = out_prefix + "_" + zero_pad(chunk_index, 4) + ext;
    try {
      save_chunk(path, format, columns, reverse_dir, exposure_ms);
      std::cerr << "Saved " << path << " (" << columns.size() << "x"
                << Mightex::Pixels << ", "
                << (columns.size() * bytes_per_frame) / 1024 << " KiB)\n";
    } catch (const std::exception &e) {
      std::cerr << "Failed saving '" << path << "': " << e.what() << "\n";
    }
    columns.clear();
    chunk_index++;
  };

  auto start_time = std::chrono::steady_clock::now();
  auto next_frame_at = start_time;

  while (!g_stop_requested) {
    if (do_frames && frames_captured_total >= frames_target)
      break;
    if (do_duration) {
      double elapsed = std::chrono::duration<double>(
                            std::chrono::steady_clock::now() - start_time)
                            .count();
      if (elapsed >= duration_s)
        break;
    }

    try {
      while (cam->buffered_frame_count() == 0) {
        if (g_stop_requested)
          break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }
    } catch (const std::exception &e) {
      std::cerr << "Error reading buffer count, stopping: " << e.what()
                << "\n";
      break;
    }
    if (g_stop_requested)
      break;

    try {
      cam->read_frame();
    } catch (const std::exception &e) {
      std::cerr << "Failed reading frame, stopping: " << e.what() << "\n";
      break;
    }

    auto raw = cam->raw_frame();
    columns.emplace_back(raw.begin(), raw.end());
    frames_captured_total++;

    if (do_max_mem && columns.size() * bytes_per_frame > max_mem_bytes)
      flush_chunk();

    std::cerr << "\rCaptured " << frames_captured_total << " frame(s)..."
               << std::flush;

    next_frame_at +=
        std::chrono::duration_cast<std::chrono::steady_clock::duration>(
            std::chrono::duration<double>(1.0 / fps));
    auto now = std::chrono::steady_clock::now();
    if (next_frame_at > now)
      std::this_thread::sleep_for(next_frame_at - now);
    else
      next_frame_at = now; // fell behind: don't accumulate drift
  }
  std::cerr << "\n";

  if (frames_captured_total == 0)
    std::cerr << "No frames captured; nothing saved.\n";
  else
    flush_chunk();

  return EXIT_SUCCESS;
}
