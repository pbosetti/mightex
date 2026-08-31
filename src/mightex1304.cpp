/**
 * @file mightex1304.cpp
 * @brief Thin C ABI wrapper around the header-only Mightex::Camera C++
 * library (include/mightex/mightex.hpp). Every mightex_* entry point below
 * catches at the C/C++ boundary and translates back to the original
 * NULL/mtx_result_t contract declared in mightex1304.h -- that header, and
 * everything it declares, is unchanged.
 */
#include "mightex1304.h"
#include <mightex/mightex.hpp>

//   ____  _        _   _
//  / ___|| |_ __ _| |_(_) ___ ___
//  \___ \| __/ _` | __| |/ __/ __|
//   ___) | || (_| | |_| | (__\__ \
//  |____/ \__\__,_|\__|_|\___|___/

struct mightex {
  Mightex::Camera cam;
  mightex_filter_t *filter_fn;
  mightex_estimator_t *estimator_fn;
};

// Default filter/estimator, reimplemented against the public Camera
// accessors so mightex_set_filter(m, NULL)/mightex_reset_filter(m) keep
// their original two-state-plus-default semantics for C callers.
static void wrapper_filter_dark(mightex_t *m, uint16_t *const data,
                                 uint16_t len, void * /*ud*/) {
  uint16_t dark = m->cam.dark_mean();
  for (uint16_t i = 0; i < len; i++)
    data[i] = data[i] < dark ? 0 : static_cast<uint16_t>(data[i] - dark);
}

static double wrapper_estimator_center(mightex_t *m, uint16_t *const data,
                                        uint16_t len, void * /*ud*/) {
  double num = 0, den = 0;
  auto thr = static_cast<uint16_t>(m->cam.dark_mean() * 3);
  for (uint16_t i = 0; i < len; i++) {
    if (data[i] < thr)
      continue;
    num += static_cast<double>(i) * data[i];
    den += data[i];
  }
  return den > 0 ? num / den : 0.0;
}

//   __  __      _   _               _
//  |  \/  | ___| |_| |__   ___   __| |___
//  | |\/| |/ _ \ __| '_ \ / _ \ / _` / __|
//  | |  | |  __/ |_| | | | (_) | (_| \__ \
//  |_|  |_|\___|\__|_| |_|\___/ \__,_|___/

mightex_t *mightex_new(void) {
  try {
    return new mightex_t{Mightex::Camera(), wrapper_filter_dark,
                          wrapper_estimator_center};
  } catch (const std::exception &) {
    return nullptr;
  }
}

void mightex_close(mightex_t *m) { delete m; }

mtx_result_t mightex_set_mode(mightex_t *m, mtx_mode_t mode) {
  try {
    m->cam.set_mode(static_cast<Mightex::Mode>(mode));
    return MTX_OK;
  } catch (const std::exception &) {
    return MTX_FAIL;
  }
}

mtx_result_t mightex_set_exptime(mightex_t *m, float t) {
  try {
    m->cam.set_exposure_time(t);
    return MTX_OK;
  } catch (const std::exception &) {
    return MTX_FAIL;
  }
}

int mightex_get_buffer_count(mightex_t *m) {
  try {
    return m->cam.buffered_frame_count();
  } catch (const std::exception &) {
    return -1;
  }
}

mtx_result_t mightex_read_frame(mightex_t *m) {
  try {
    m->cam.read_frame();
    return MTX_OK;
  } catch (const std::exception &) {
    return MTX_FAIL;
  }
}

void mightex_gpio_write(mightex_t *m, BYTE reg, BYTE val) {
  try {
    m->cam.gpio_write(reg, val);
  } catch (const std::exception &) {
  }
}

int mightex_gpio_read(mightex_t *m, BYTE reg) {
  try {
    return m->cam.gpio_read(reg);
  } catch (const std::exception &) {
    return -1;
  }
}

void mightex_apply_filter(mightex_t *m, void *ud) {
  if (m->filter_fn) {
    auto d = m->cam.frame();
    m->filter_fn(m, d.data(), static_cast<uint16_t>(d.size()), ud);
  }
}

double mightex_apply_estimator(mightex_t *m, void *ud) {
  if (m->estimator_fn) {
    auto d = m->cam.frame();
    return m->estimator_fn(m, d.data(), static_cast<uint16_t>(d.size()), ud);
  }
  return 0.0;
}

void mightex_set_filter(mightex_t *m, mightex_filter_t *filter) {
  m->filter_fn = filter;
}

void mightex_reset_filter(mightex_t *m) { m->filter_fn = wrapper_filter_dark; }

void mightex_set_estimator(mightex_t *m, mightex_estimator_t *estimator) {
  m->estimator_fn = estimator;
}

void mightex_reset_estimator(mightex_t *m) {
  m->estimator_fn = wrapper_estimator_center;
}

//      _
//     / \   ___ ___ ___  ___ ___  ___  _ __ ___
//    / _ \ / __/ __/ _ \/ __/ __|/ _ \| '__/ __|
//   / ___ \ (_| (_|  __/\__ \__ \ (_) | |  \__ \
//  /_/   \_\___\___\___||___/___/\___/|_|  |___/

char *mightex_serial_no(mightex_t *m) {
  return const_cast<char *>(m->cam.serial_no_cstr());
}

char *mightex_version(mightex_t *m) {
  return const_cast<char *>(m->cam.version().c_str());
}

char *mightex_sw_version() {
  return const_cast<char *>("Mightex1304 v." GIT_COMMIT_HASH " for " CMAKE_PLATFORM
                             ", " CMAKE_BUILD_TYPE " build.");
}

uint16_t *mightex_frame_p(mightex_t *m) { return m->cam.frame().data(); }

uint16_t *mightex_raw_frame_p(mightex_t *m) {
  return const_cast<uint16_t *>(m->cam.raw_frame().data());
}

uint16_t mightex_frame_timestamp(mightex_t *m) { return m->cam.timestamp(); }

uint16_t mightex_dark_mean(mightex_t *m) { return m->cam.dark_mean(); }

uint16_t mightex_pixel_count(mightex_t *) {
  return static_cast<uint16_t>(Mightex::Pixels);
}

uint16_t mightex_dark_pixel_count(mightex_t *) {
  return static_cast<uint16_t>(Mightex::DarkPixels);
}
