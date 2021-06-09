#include "mightex1304.h"
#ifdef _WIN32
#pragma comment(lib, "Ws2_32.lib")
#include <winsock.h>
#else
#include <arpa/inet.h>
#endif
#include <assert.h>
#include <libusb-1.0/libusb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <stdint.h>
#else
#include <unistd.h>
#endif // _WIN32

#define USB_IDVENDOR 0x04B4
#define USB_IDPRODUCT 0x0328
#define MTX_TIMEOUT 2000
#define MTX_EP_CMD 0x01
#define MTX_EP_REPLY 0x81
#define MTX_EP_FRAME 0x82

#define MTX_CMD_FIRMWARE 0x01
#define MTX_CMD_INFO 0x21
#define MTX_CMD_MODE 0x30
#define MTX_CMD_EXPTIME 0x31
#define MTX_CMD_BUFFEREDFRAMES 0x33
#define MTX_CMD_GETBUFFEREDDATA 0x34
#define MTX_CMD_GPIOWRITE 0x40
#define MTX_CMD_GPIOREAD 0x41

#define STRING_LENGTH 14

typedef union {
#ifdef _WIN32
  struct di {
#else
  struct __attribute__((__packed__)) di {
#endif
    BYTE rc;
    BYTE len;
    BYTE config_revision;
    BYTE module_no[STRING_LENGTH];
    BYTE serial_no[STRING_LENGTH];
    BYTE manuafacture_date[STRING_LENGTH];
  } di;
  BYTE buf[sizeof(struct di)];
} device_info_t;

typedef union {
#ifdef _WIN32
  struct ver {
#else
  struct __attribute__((__packed__)) ver {
#endif
    BYTE rc;
    BYTE len;
    BYTE major, minor, rev;
  } version;
  BYTE buf[sizeof(struct di)];
} device_version_t;

typedef union {
#ifdef _WIN32
  struct frame {
#else
  struct __attribute__((__packed__)) frame {
#endif
    uint16_t _dummy1[16];
    uint16_t light_shield[13];
    uint16_t _reserved[3];
    uint16_t image_data[3648];
    uint16_t _dummy2[14];
    uint16_t _padding[138];
    uint16_t time_stamp;
    uint16_t exposure_time;
    uint16_t trigger_occurred;
    uint16_t trigger_event_count;
    uint16_t _padding2[4];
  } frame;
  BYTE buf[sizeof(struct frame)];
} ccd_frames_t;

typedef struct mightex {
  libusb_device *dev;
  libusb_device_handle *handle;
  libusb_context *ctx;
  struct libusb_device_descriptor *desc;
  unsigned char manufacturer[256];
  unsigned char product[256];
  unsigned int timeout;
  device_info_t device_info;
  device_version_t device_version;
  ccd_frames_t frames[4];
  uint16_t data[MTX_PIXELS];
  uint16_t dark_mean;
  char version[12];
  char sw_version[64];
  mightex_filter_t *filter;
  mightex_estimator_t *estimator;
} mightex_t;

//   ____  _        _   _
//  / ___|| |_ __ _| |_(_) ___ ___
//  \___ \| __/ _` | __| |/ __/ __|
//   ___) | || (_| | |_| | (__\__ \
//  |____/ \__\__,_|\__|_|\___|___/

static void filter_dark(mightex_t *m, uint16_t *const data, uint16_t len,
                        void *ud) {
  register uint16_t i = 0;
  for (i = 0; i < MTX_PIXELS; i++) {
    data[i] = data[i] < m->dark_mean ? 0 : data[i] - m->dark_mean;
  }
}

static double estimator_center(mightex_t *m, uint16_t *const data, uint16_t len,
                               void *ud) {
  register uint16_t i = 0;
  double num = 0, den = 0;
  uint16_t thr = m->dark_mean * 3;
  for (i = 0; i < MTX_PIXELS; i++) {
    if (data[i] < thr)
      continue;
    num += (i * data[i]);
    den += data[i];
  }
  return num / den;
}

static mtx_result_t mightex_send(mightex_t *m, BYTE *const buf, int len) {
  int rc;
  rc = libusb_bulk_transfer(m->handle, MTX_EP_CMD, buf, len, NULL, m->timeout);
  if (rc != LIBUSB_SUCCESS) {
    fprintf(stderr, "Error on send: %s\n", libusb_error_name(rc));
    return MTX_FAIL;
  }
  return MTX_OK;
}

static mtx_result_t mightex_receive(mightex_t *m, BYTE *const buf, int len) {
  int rc;
  rc =
      libusb_bulk_transfer(m->handle, MTX_EP_REPLY, buf, len, NULL, m->timeout);
  if (rc != LIBUSB_SUCCESS) {
    fprintf(stderr, "Error on receive: %s\n", libusb_error_name(rc));
    return MTX_FAIL;
  }
  return (mtx_result_t)buf[0];
}

static mtx_result_t mightex_get_version(mightex_t *m) {
  int rc;
  device_version_t *dv = &m->device_version;
  // request
  memset(dv, 0, sizeof(device_info_t));
  dv->buf[0] = MTX_CMD_FIRMWARE;
  dv->buf[1] = 0x01;
  dv->buf[2] = 0x02;
  rc = mightex_send(m, dv->buf, 3);
  if (rc != MTX_OK)
    return MTX_FAIL;
  // reply
  memset(dv, 0, sizeof(device_info_t));
  rc = mightex_receive(m, dv->buf, sizeof(dv->version));
  snprintf(m->version, sizeof(m->version), "%d.%d.%d", dv->version.major,
           dv->version.minor, dv->version.rev);
  return rc;
}

static mtx_result_t mightex_get_info(mightex_t *m) {
  int rc;
  device_info_t *di = &m->device_info;
  // request
  memset(di, 0, sizeof(device_info_t));
  di->buf[0] = MTX_CMD_INFO;
  di->buf[1] = 0x01;
  di->buf[2] = 0x00;
  rc = mightex_send(m, di->buf, 3);
  if (rc != MTX_OK)
    return MTX_FAIL;
  // reply
  memset(di, 0, sizeof(device_info_t));
  rc = mightex_receive(m, di->buf, sizeof(device_info_t));
  return rc;
}

static mtx_result_t mightex_prepare_buffered_data(mightex_t *m, BYTE n) {
  BYTE buf[3];
  buf[0] = MTX_CMD_GETBUFFEREDDATA;
  buf[1] = 0x01;
  buf[2] = n;
  return mightex_send(m, buf, sizeof(buf));
}

//   __  __      _   _               _
//  |  \/  | ___| |_| |__   ___   __| |___
//  | |\/| |/ _ \ __| '_ \ / _ \ / _` / __|
//  | |  | |  __/ |_| | | | (_) | (_| \__ \
//  |_|  |_|\___|\__|_| |_|\___/ \__,_|___/

mightex_t *mightex_new() {
  mightex_t *m = malloc(sizeof(mightex_t));
  int rc;
  ssize_t cnt, i = 0;
  libusb_device **devs;

  m->timeout = MTX_TIMEOUT;
  m->dark_mean = 0;
  m->desc = malloc(sizeof(m->desc));
  m->filter = filter_dark;
  m->estimator = estimator_center;
  snprintf(m->sw_version, sizeof(m->sw_version), "%s %s %s", GIT_COMMIT_HASH,
           CMAKE_PLATFORM, CMAKE_BUILD_TYPE);

  rc = libusb_init(&m->ctx);
  if (rc < 0)
    return NULL;

  rc =
      libusb_set_option(m->ctx, LIBUSB_OPTION_LOG_LEVEL, LIBUSB_LOG_LEVEL_NONE);
  if (rc != LIBUSB_SUCCESS)
    fprintf(stderr, "> Could not set log level (%s).\n", libusb_error_name(rc));

  cnt = libusb_get_device_list(m->ctx, &devs);
  if (cnt < 0) {
    fprintf(stderr, "> No devices available.\n");
    libusb_exit(m->ctx);
    return NULL;
  }

  while ((m->dev = devs[i++]) != NULL) {
    rc = libusb_get_device_descriptor(m->dev, m->desc);
    if (rc < 0) {
      fprintf(stderr, ">>> FATAL: Failed to get device descriptor (%s).",
              libusb_error_name(rc));
      libusb_exit(m->ctx);
      continue;
    }
    if (m->desc->idVendor == USB_IDVENDOR &&
        m->desc->idProduct == USB_IDPRODUCT) {
      device_info_t b;
      memset(&b, 0, sizeof(b));
      rc = libusb_open(m->dev, &m->handle);
      if (rc != LIBUSB_SUCCESS) {
        fprintf(stderr, ">>> FATAL: Could not open device (%s)\n",
                libusb_error_name(rc));
#ifdef _WIN32
        fprintf(
            stderr,
            "    Perhaps WinUSB driver has not been installed and selected?\n");
#endif
        libusb_exit(m->ctx);
        return NULL;
      }

      rc = libusb_reset_device(m->handle);
      if (rc != LIBUSB_SUCCESS)
        fprintf(stderr, ">> Could not reset device (%s)\n",
                libusb_error_name(rc));

      rc = libusb_set_auto_detach_kernel_driver(m->handle, 1);
      if (rc != LIBUSB_ERROR_NOT_SUPPORTED)
        fprintf(stderr, ">> Could not set auto-detach (%s)\n",
                libusb_error_name(rc));

      rc = libusb_claim_interface(m->handle, 0);
      if (rc != LIBUSB_SUCCESS) {
        fprintf(stderr, ">>> FATAL: Could not claim device interface (%s)\n",
                libusb_error_name(rc));
        libusb_close(m->handle);
        libusb_exit(m->ctx);
        return NULL;
      }

      rc = libusb_get_string_descriptor_ascii(m->handle, m->desc->iManufacturer,
                                              m->manufacturer,
                                              sizeof(m->manufacturer));
      if (rc <= 0)
        fprintf(stderr, ">> Could nor read device manufacturer (%s)\n",
                libusb_error_name(rc));

      rc = libusb_get_string_descriptor_ascii(m->handle, m->desc->iProduct,
                                              m->product, sizeof(m->product));
      if (rc <= 0)
        fprintf(stderr, ">> Could nor read device name (%s)\n",
                libusb_error_name(rc));

      fprintf(stderr, "> Found device: %s - %s\n", m->manufacturer, m->product);

      mightex_get_version(m);
      fprintf(stderr, "> Version: %s\n", mightex_version(m));

      mightex_get_info(m);
      fprintf(stderr, "> SerialNo.: %s\n", mightex_serial_no(m));

      break;
    } else {
      m->dev = NULL;
    }
  }
  libusb_free_device_list(devs, 1);
  if (m->dev == NULL) {
    m = NULL;
  }
  return m;
}

void mightex_close(mightex_t *m) {
  int rc;
  if (!m)
    return;
  if (m->handle) {
    rc = libusb_release_interface(m->handle, 0);
    if (rc != LIBUSB_SUCCESS)
      fprintf(stderr, ">> Could not release interface (%s)\n",
              libusb_error_name(rc));
    libusb_close(m->handle);
  }
  libusb_exit(m->ctx);
  free(m);
}

mtx_result_t mightex_set_mode(mightex_t *m, mtx_mode_t mode) {
  BYTE mode_b = (BYTE)mode;
  BYTE buf[3] = {MTX_CMD_MODE, 0x01, mode_b};
  return mightex_send(m, buf, sizeof(buf));
}

// t is in ms
mtx_result_t mightex_set_exptime(mightex_t *m, float t) {
  BYTE buf[4];
  uint16_t val = htons((uint16_t)(t * 10));
  buf[0] = MTX_CMD_EXPTIME;
  buf[1] = 0x02;
  memcpy(buf + 2, &val, sizeof(val));
  return mightex_send(m, buf, sizeof(buf));
}

int mightex_get_buffer_count(mightex_t *m) {
  int rc;
  BYTE buf[3] = {MTX_CMD_BUFFEREDFRAMES, 0x01, 0x00};
  mightex_send(m, buf, 2);
  rc = mightex_receive(m, buf, sizeof(buf));
  if (rc <= 0)
    return rc;
  return (int)buf[2];
}

mtx_result_t mightex_read_frame(mightex_t *m) {
  int i, rc;
  mightex_prepare_buffered_data(m, 1);
  rc = libusb_bulk_transfer(m->handle, MTX_EP_FRAME, m->frames[0].buf,
                            sizeof(m->frames[0].frame), NULL, m->timeout);
  if (rc != LIBUSB_SUCCESS) {
    return MTX_FAIL;
  }
  m->dark_mean = 0;
  for (i = 0; i < MTX_DARK_PIXELS; i++) {
    m->dark_mean += m->frames[0].frame.light_shield[i];
  }
  m->dark_mean /= MTX_DARK_PIXELS;
  memcpy(m->data, m->frames[0].frame.image_data, MTX_PIXELS * sizeof(uint16_t));
  return MTX_OK;
}

void mightex_gpio_write(mightex_t *m, BYTE reg, BYTE val) {
  BYTE buf[4] = {MTX_CMD_GPIOWRITE, 0x02, reg, val};
  mightex_send(m, buf, sizeof(buf));
}

BYTE mightex_gpio_read(mightex_t *m, BYTE reg) {
  int rc;
  BYTE buf[3] = {MTX_CMD_GPIOREAD, 0x03, reg};
  mightex_send(m, buf, sizeof(buf));
  rc = mightex_receive(m, buf, sizeof(buf));
  if (rc <= 0)
    return -1;
  return buf[2];
}

void mightex_apply_filter(mightex_t *m, void *ud) {
  if (m->filter)
    m->filter(m, m->data, MTX_PIXELS, ud);
}

double mightex_apply_estimator(mightex_t *m, void *ud) {
  if (m->estimator)
    return m->estimator(m, m->data, MTX_PIXELS, ud);
  else
    return 0.0;
}

//      _
//     / \   ___ ___ ___  ___ ___  ___  _ __ ___
//    / _ \ / __/ __/ _ \/ __/ __|/ _ \| '__/ __|
//   / ___ \ (_| (_|  __/\__ \__ \ (_) | |  \__ \
//  /_/   \_\___\___\___||___/___/\___/|_|  |___/

char *mightex_serial_no(mightex_t *m) {
  return (char *)m->device_info.di.serial_no;
}

char *mightex_version(mightex_t *m) { return m->version; }

char *mightex_sw_version() { return "Mightex1304 v." GIT_COMMIT_HASH " for " CMAKE_PLATFORM ", " CMAKE_BUILD_TYPE " build."; }

uint16_t *mightex_frame_p(mightex_t *m) { return m->data; }

uint16_t *mightex_raw_frame_p(mightex_t *m) {
  return m->frames[0].frame.image_data;
}

uint16_t mightex_frame_timestamp(mightex_t *m) {
  return m->frames[0].frame.time_stamp;
}

uint16_t mightex_dark_mean(mightex_t *m) { return m->dark_mean; }

uint16_t mightex_pixel_count(mightex_t *m) { return MTX_PIXELS; }

uint16_t mightex_dark_pixel_count(mightex_t *m) { return MTX_DARK_PIXELS; }

void mightex_set_filter(mightex_t *m, mightex_filter_t *filter) {
  m->filter = filter;
}

void mightex_reset_filter(mightex_t *m) { m->filter = filter_dark; }

void mightex_set_estimator(mightex_t *m, mightex_estimator_t *estimator) {
  m->estimator = estimator;
}

void mightex_reset_estimator(mightex_t *m) { m->estimator = estimator_center; }
