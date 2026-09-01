/**
 * @file mightex.hpp
 * @brief Header-only C++20 driver for the Mightex TCE/TCN-1304-U line CCD
 * camera.
 *
 * RAII device lifetime (the constructor either fully succeeds or throws;
 * there is nothing to leak on any failure path) and exceptions for error
 * reporting. Talks to the device over libusb, so linking libusb-1.0 is
 * still required even though this header has no separate translation unit.
 *
 * @copyright Copyright (c) 2021 Paolo Bosetti
 */
#pragma once

#include <libusb.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <iostream>
#include <memory>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace Mightex {

/// Number of standard (light-sensitive) pixels on the TCE/TCN-1304-U sensor.
inline constexpr std::size_t Pixels = 3648;
/// Number of "light-shield" (optically black) pixels used to estimate dark
/// current; their average is subtracted from the standard pixels.
inline constexpr std::size_t DarkPixels = 13;

/// Maximum raw pixel (ADC) value before the datasheet considers the sensor
/// "Over Exposured" -- the practical full-scale ceiling for this camera,
/// well below the 16-bit field's own 0xFFFF range.
inline constexpr uint16_t MaxPixelValue = 0xC000;

/// Camera operating mode.
enum class Mode : uint8_t { Normal = 0, Trigger = 1 };

/// A libusb call failed at the transport level.
class UsbError : public std::runtime_error {
public:
  UsbError(std::string_view msg, int libusb_rc)
      : std::runtime_error(std::string(msg) + ": " +
                            libusb_error_name(libusb_rc)),
        _code(libusb_rc) {}
  /// The underlying libusb_error code.
  int code() const noexcept { return _code; }

private:
  int _code;
};

/// No matching camera (idVendor/idProduct) was found among attached USB
/// devices.
class DeviceNotFoundError : public std::runtime_error {
public:
  explicit DeviceNotFoundError(std::string_view msg)
      : std::runtime_error(std::string(msg)) {}
};

namespace detail {

// Wire-format structs mirroring the Mightex CCD Line Camera USB Protocol
// datasheet exactly (byte for byte). #pragma pack keeps them unpadded on
// every compiler (GCC/Clang/MSVC alike) -- the previous C version only
// applied __attribute__((packed)) outside of _WIN32, silently relying on
// these particular field layouts never needing padding on Windows.
#pragma pack(push, 1)
struct DeviceInfoFields {
  uint8_t rc;
  uint8_t len;
  uint8_t config_revision;
  uint8_t module_no[14];
  uint8_t serial_no[14];
  uint8_t manufacture_date[14];
};

struct DeviceVersionFields {
  uint8_t rc;
  uint8_t len;
  uint8_t major, minor, rev;
};

struct CcdFrameFields {
  uint16_t dummy1[16];
  uint16_t light_shield[DarkPixels];
  uint16_t reserved[3];
  uint16_t image_data[Pixels];
  uint16_t dummy2[14];
  uint16_t padding[138];
  uint16_t time_stamp;
  uint16_t exposure_time;
  uint16_t trigger_occurred;
  uint16_t trigger_event_count;
  uint16_t padding2[4];
};
#pragma pack(pop)

union DeviceInfo {
  DeviceInfoFields di;
  uint8_t buf[sizeof(DeviceInfoFields)];
};

union DeviceVersion {
  DeviceVersionFields version;
  uint8_t buf[sizeof(DeviceVersionFields)];
};

union CcdFrame {
  CcdFrameFields frame;
  uint8_t buf[sizeof(CcdFrameFields)];
};

} // namespace detail

/**
 * @brief RAII handle to a single Mightex TCE/TCN-1304-U camera.
 *
 * Move-only: owns a libusb context and device handle. Every fallible
 * operation throws (UsbError / DeviceNotFoundError) instead of returning a
 * sentinel value.
 */
class Camera {
public:
  /// A filter runs in place over the (mutable) working copy of the frame.
  using Filter = std::function<void(std::span<uint16_t>)>;
  /// An estimator reduces the working copy of the frame to a single value.
  using Estimator = std::function<double(std::span<const uint16_t>)>;

  /// Opens the first attached device matching the Mightex vendor/product ID.
  /// @throws DeviceNotFoundError if no matching device is attached.
  /// @throws UsbError if libusb itself fails (init, open, claim interface).
  Camera() {
    libusb_context *ctx_raw = nullptr;
    int rc = libusb_init(&ctx_raw);
    if (rc < 0)
      throw UsbError("libusb_init failed", rc);
    _ctx.reset(ctx_raw);

    rc = libusb_set_option(_ctx.get(), LIBUSB_OPTION_LOG_LEVEL,
                            LIBUSB_LOG_LEVEL_NONE);
    if (rc != LIBUSB_SUCCESS)
      std::cerr << "> Could not set log level (" << libusb_error_name(rc)
                << ").\n";

    libusb_device **devs_raw = nullptr;
    ssize_t cnt = libusb_get_device_list(_ctx.get(), &devs_raw);
    if (cnt < 0) {
      std::cerr << "> No devices available.\n";
      throw UsbError("libusb_get_device_list failed",
                      static_cast<int>(cnt));
    }
    DeviceListPtr devs(devs_raw);

    libusb_device *found = nullptr;
    libusb_device_descriptor desc{};
    for (ssize_t i = 0; devs.get()[i] != nullptr; ++i) {
      int drc = libusb_get_device_descriptor(devs.get()[i], &desc);
      if (drc < 0) {
        std::cerr << ">>> FATAL: Failed to get device descriptor ("
                  << libusb_error_name(drc) << ").\n";
        continue;
      }
      if (desc.idVendor == VendorId && desc.idProduct == ProductId) {
        found = devs.get()[i];
        break;
      }
    }
    if (!found)
      throw DeviceNotFoundError("No Mightex TCE-1304-U camera found");

    libusb_device_handle *handle_raw = nullptr;
    rc = libusb_open(found, &handle_raw);
    if (rc != LIBUSB_SUCCESS) {
#ifdef _WIN32
      std::cerr
          << "    Perhaps WinUSB driver has not been installed and selected?\n";
#endif
      throw UsbError("Could not open device", rc);
    }
    _handle.reset(handle_raw);

    rc = libusb_reset_device(_handle.get());
    if (rc != LIBUSB_SUCCESS)
      std::cerr << ">> Could not reset device (" << libusb_error_name(rc)
                << ")\n";

    rc = libusb_set_auto_detach_kernel_driver(_handle.get(), 1);
    if (rc != LIBUSB_SUCCESS && rc != LIBUSB_ERROR_NOT_SUPPORTED)
      std::cerr << ">> Could not set auto-detach (" << libusb_error_name(rc)
                << ")\n";

    rc = libusb_claim_interface(_handle.get(), 0);
    if (rc != LIBUSB_SUCCESS)
      throw UsbError("Could not claim device interface", rc);

    unsigned char manufacturer[256]{}, product[256]{};
    int mrc = libusb_get_string_descriptor_ascii(
        _handle.get(), desc.iManufacturer, manufacturer,
        sizeof(manufacturer));
    if (mrc <= 0)
      std::cerr << ">> Could not read device manufacturer ("
                << libusb_error_name(mrc) << ")\n";
    int prc = libusb_get_string_descriptor_ascii(
        _handle.get(), desc.iProduct, product, sizeof(product));
    if (prc <= 0)
      std::cerr << ">> Could not read device name (" << libusb_error_name(prc)
                << ")\n";
    std::cerr << "> Found device: " << manufacturer << " - " << product
              << "\n";

    // Firmware version / serial number are diagnostic, non-fatal: the
    // original C library never checked their result either.
    try {
      fetch_version();
      std::cerr << "> Version: " << _version << "\n";
      fetch_info();
      std::cerr << "> SerialNo.: " << _serial_no << "\n";
    } catch (const UsbError &e) {
      std::cerr << ">> " << e.what() << "\n";
    }
  }

  ~Camera() {
    if (_handle) {
      int rc = libusb_release_interface(_handle.get(), 0);
      if (rc != LIBUSB_SUCCESS)
        std::cerr << ">> Could not release interface ("
                  << libusb_error_name(rc) << ")\n";
    }
    // _handle then _ctx clean up via their deleters (declaration order).
  }

  Camera(const Camera &) = delete;
  Camera &operator=(const Camera &) = delete;
  Camera(Camera &&) noexcept = default;
  Camera &operator=(Camera &&) noexcept = default;

  /// Exposure time in milliseconds (0.1 ms resolution).
  void set_exposure_time(float ms) {
    uint16_t val = static_cast<uint16_t>(ms * 10);
    uint8_t buf[4] = {CmdExpTime, 0x02, static_cast<uint8_t>(val >> 8),
                       static_cast<uint8_t>(val & 0xFF)};
    send(buf);
  }

  void set_mode(Mode mode) {
    uint8_t buf[3] = {CmdMode, 0x01, static_cast<uint8_t>(mode)};
    send(buf);
  }

  /// Number of frames currently held in the camera's on-board buffer (0-4).
  /// @throws UsbError on any transport or device-reported failure -- unlike
  /// the C API this ported from, there is no ambiguous sentinel value: a
  /// real reading is always >= 0.
  int buffered_frame_count() {
    uint8_t buf[3] = {CmdBufferedFrames, 0x01, 0x00};
    send(buf);
    uint8_t status = receive(buf);
    if (status != 1)
      throw UsbError("device reported an error reading buffer count", 0);
    return buf[2];
  }

  /// Reads one frame from the camera into the internal working/raw buffers.
  /// @throws UsbError on failure or a short transfer.
  void read_frame() {
    prepare_buffered_data(1);
    int actual = 0;
    int rc = libusb_bulk_transfer(_handle.get(), EpFrame, _frame.buf,
                                   static_cast<int>(sizeof(_frame.frame)),
                                   &actual, Timeout);
    if (rc != LIBUSB_SUCCESS ||
        actual != static_cast<int>(sizeof(_frame.frame)))
      throw UsbError("read_frame failed", rc);

    uint32_t dark_sum = 0;
    for (uint16_t v : _frame.frame.light_shield)
      dark_sum += v;
    _dark_mean = static_cast<uint16_t>(dark_sum / DarkPixels);
    std::copy(std::begin(_frame.frame.image_data),
              std::end(_frame.frame.image_data), _data.begin());
  }

  /// Applies the current filter (default: subtract the dark-pixel mean, set
  /// via set_filter(), or none if disabled via set_filter({})) in place over
  /// the working copy returned by frame().
  void apply_filter() {
    switch (_filter_state) {
    case FilterState::Disabled:
      return;
    case FilterState::Custom:
      _filter(_data);
      return;
    case FilterState::Default:
      for (uint16_t &v : _data)
        v = v < _dark_mean ? uint16_t{0}
                            : static_cast<uint16_t>(v - _dark_mean);
      return;
    }
  }

  /// Applies the current estimator (default: intensity-weighted centroid)
  /// over the (possibly filtered) working copy of the frame.
  double apply_estimator() {
    switch (_estimator_state) {
    case EstimatorState::Disabled:
      return 0.0;
    case EstimatorState::Custom:
      return _estimator(_data);
    case EstimatorState::Default: {
      double num = 0, den = 0;
      auto thr = static_cast<uint16_t>(_dark_mean * 3);
      for (std::size_t i = 0; i < _data.size(); ++i) {
        if (_data[i] < thr)
          continue;
        num += static_cast<double>(i) * _data[i];
        den += _data[i];
      }
      return den > 0 ? num / den : 0.0;
    }
    }
    return 0.0;
  }

  /// The working (filterable) copy of the last frame read.
  std::span<const uint16_t> frame() const noexcept { return _data; }
  std::span<uint16_t> frame() noexcept { return _data; }
  /// The untouched raw copy of the last frame read.
  std::span<const uint16_t> raw_frame() const noexcept {
    return _frame.frame.image_data;
  }
  uint16_t timestamp() const noexcept { return _frame.frame.time_stamp; }
  uint16_t dark_mean() const noexcept { return _dark_mean; }
  static constexpr std::size_t pixel_count() noexcept { return Pixels; }
  static constexpr std::size_t dark_pixel_count() noexcept {
    return DarkPixels;
  }

  std::string_view serial_no() const noexcept { return _serial_no; }
  /// Same as serial_no(), as a NUL-terminated C string (for C interop).
  const char *serial_no_cstr() const noexcept { return _serial_no.c_str(); }
  /// Firmware version, as "major.minor.rev".
  const std::string &version() const noexcept { return _version; }

  /// Sets a custom filter, or disables filtering entirely if `f` is empty.
  void set_filter(Filter f) {
    if (f) {
      _filter = std::move(f);
      _filter_state = FilterState::Custom;
    } else {
      _filter_state = FilterState::Disabled;
    }
  }
  /// Restores the default dark-subtraction filter.
  void reset_filter() {
    _filter_state = FilterState::Default;
    _filter = nullptr;
  }
  /// Sets a custom estimator, or disables it (apply_estimator() returns 0.0)
  /// if `e` is empty.
  void set_estimator(Estimator e) {
    if (e) {
      _estimator = std::move(e);
      _estimator_state = EstimatorState::Custom;
    } else {
      _estimator_state = EstimatorState::Disabled;
    }
  }
  /// Restores the default centroid estimator.
  void reset_estimator() {
    _estimator_state = EstimatorState::Default;
    _estimator = nullptr;
  }

  void gpio_write(uint8_t reg, uint8_t val) {
    uint8_t buf[4] = {CmdGpioWrite, 0x02, reg, val};
    send(buf);
  }

  /// @throws UsbError on failure (no more ambiguous -1/255 sentinel).
  uint8_t gpio_read(uint8_t reg) {
    uint8_t buf[3] = {CmdGpioRead, 0x01, reg};
    send(buf);
    receive(buf);
    return buf[2];
  }

private:
  enum class FilterState { Default, Custom, Disabled };
  enum class EstimatorState { Default, Custom, Disabled };

  struct ContextDeleter {
    void operator()(libusb_context *ctx) const noexcept {
      if (ctx)
        libusb_exit(ctx);
    }
  };
  struct HandleDeleter {
    void operator()(libusb_device_handle *h) const noexcept {
      if (h)
        libusb_close(h);
    }
  };
  struct DeviceListDeleter {
    void operator()(libusb_device **devs) const noexcept {
      if (devs)
        libusb_free_device_list(devs, 1);
    }
  };
  using ContextPtr = std::unique_ptr<libusb_context, ContextDeleter>;
  using HandlePtr = std::unique_ptr<libusb_device_handle, HandleDeleter>;
  using DeviceListPtr =
      std::unique_ptr<libusb_device *, DeviceListDeleter>;

  static constexpr uint16_t VendorId = 0x04B4;
  static constexpr uint16_t ProductId = 0x0328;
  static constexpr unsigned Timeout = 2000; // ms
  static constexpr uint8_t EpCmd = 0x01, EpReply = 0x81, EpFrame = 0x82;
  static constexpr uint8_t CmdFirmware = 0x01, CmdInfo = 0x21, CmdMode = 0x30,
                            CmdExpTime = 0x31, CmdBufferedFrames = 0x33,
                            CmdGetBufferedData = 0x34, CmdGpioWrite = 0x40,
                            CmdGpioRead = 0x41;

  /// Sends a command buffer on the OUT command endpoint.
  /// @throws UsbError on transport failure.
  void send(std::span<const uint8_t> buf) {
    int actual = 0;
    // libusb's C API takes a non-const pointer for both directions.
    int rc = libusb_bulk_transfer(
        _handle.get(), EpCmd, const_cast<uint8_t *>(buf.data()),
        static_cast<int>(buf.size()), &actual, Timeout);
    if (rc != LIBUSB_SUCCESS)
      throw UsbError("send failed", rc);
  }

  /// Reads a reply on the IN command endpoint.
  /// @returns the reply's status byte (0x01 = OK, 0x00 = device-reported
  /// error), left for the caller to interpret.
  /// @throws UsbError on transport failure.
  uint8_t receive(std::span<uint8_t> buf) {
    int actual = 0;
    int rc = libusb_bulk_transfer(_handle.get(), EpReply, buf.data(),
                                   static_cast<int>(buf.size()), &actual,
                                   Timeout);
    if (rc != LIBUSB_SUCCESS)
      throw UsbError("receive failed", rc);
    return buf[0];
  }

  void prepare_buffered_data(uint8_t n) {
    uint8_t buf[3] = {CmdGetBufferedData, 0x01, n};
    send(buf);
  }

  void fetch_version() {
    detail::DeviceVersion dv{};
    dv.buf[0] = CmdFirmware;
    dv.buf[1] = 0x01;
    dv.buf[2] = 0x02;
    send(std::span(dv.buf).first(3));
    dv = {};
    receive(std::span(dv.buf).first(sizeof(dv.version)));
    std::ostringstream oss;
    oss << static_cast<int>(dv.version.major) << '.'
        << static_cast<int>(dv.version.minor) << '.'
        << static_cast<int>(dv.version.rev);
    _version = oss.str();
  }

  void fetch_info() {
    detail::DeviceInfo di{};
    di.buf[0] = CmdInfo;
    di.buf[1] = 0x01;
    di.buf[2] = 0x00;
    send(std::span(di.buf).first(3));
    di = {};
    uint8_t status = receive(di.buf);
    if (status != 1)
      throw UsbError("device reported an error reading device info", 0);
    _serial_no.assign(reinterpret_cast<const char *>(di.di.serial_no),
                       sizeof(di.di.serial_no));
  }

  ContextPtr _ctx;
  HandlePtr _handle;
  detail::CcdFrame _frame{};
  std::array<uint16_t, Pixels> _data{};
  uint16_t _dark_mean = 0;
  std::string _version;
  std::string _serial_no;
  FilterState _filter_state = FilterState::Default;
  Filter _filter;
  EstimatorState _estimator_state = EstimatorState::Default;
  Estimator _estimator;
};

} // namespace Mightex
