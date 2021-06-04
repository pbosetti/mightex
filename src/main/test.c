/*
 * libusb example program to list devices on the bus
 * Copyright © 2007 Daniel Drake <dsd@gentoo.org>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <arpa/inet.h>
#include <assert.h>
#include <libusb-1.0/libusb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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

#define STRING_LENGTH 14
typedef unsigned char BYTE;

typedef enum { MTX_NORMAL_MODE = 0, MTX_TRIGGER_MODE = 1 } mtx_mode_t;

typedef union {
  struct __attribute__((__packed__)) di {
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
  struct __attribute__((__packed__)) ver {
    BYTE rc;
    BYTE len;
    BYTE major, minor, rev;
  } version;
  BYTE buf[sizeof(struct di)];
} device_version_t;

typedef union {
  struct __attribute__((__packed__)) frame {
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

typedef struct {
  libusb_device *dev;
  libusb_device_handle *handle;
  libusb_context *ctx;
  struct libusb_device_descriptor *desc;
  unsigned char manufacturer[256];
  unsigned char product[256];
  unsigned int timeout;
  device_info_t device_info;
  device_version_t device_version;
  char version[12];
} mightex_t;

static int mightex_send(mightex_t *m, BYTE *buf, int len) {
  int rc;
  rc = libusb_bulk_transfer(m->handle, MTX_EP_CMD, buf, len, NULL, m->timeout);
  if (rc != LIBUSB_SUCCESS) {
    printf("Error on send: %s\n", libusb_error_name(rc));
    return rc;
  }
  return 0x01;
}

static int mightex_receive(mightex_t *m, BYTE *buf, int len) {
  int rc;
  rc =
      libusb_bulk_transfer(m->handle, MTX_EP_REPLY, buf, len, NULL, m->timeout);
  if (rc != LIBUSB_SUCCESS) {
    printf("Error on receive: %s\n", libusb_error_name(rc));
    return rc;
  }
  return (int)buf[0];
}

int mightex_get_version(mightex_t *m) {
  int rc;
  device_version_t *dv = &m->device_version;
  // request
  memset(dv, 0, sizeof(device_info_t));
  dv->buf[0] = MTX_CMD_FIRMWARE;
  rc = mightex_send(m, dv->buf, 2);
  if (rc != 0x01)
    return rc;
  // reply
  memset(dv, 0, sizeof(device_info_t));
  rc = mightex_receive(m, dv->buf, sizeof(dv->version));
  snprintf(m->version, sizeof(m->version), "%d.%d.%d", dv->version.major,
             dv->version.minor, dv->version.rev);
  return rc;
}

int mightex_get_info(mightex_t *m) {
  int rc;
  device_info_t *di = &m->device_info;
  // request
  memset(di, 0, sizeof(device_info_t));
  di->buf[0] = MTX_CMD_INFO;
  rc = mightex_send(m, di->buf, 2);
  if (rc != 0x01)
    return rc;
  // reply
  memset(di, 0, sizeof(device_info_t));
  rc = mightex_receive(m, di->buf, sizeof(device_info_t));
  return rc;
}

int mightex_set_mode(mightex_t *m, mtx_mode_t mode) {
  BYTE mode_b = (BYTE)mode;
  BYTE buf[2];
  buf[0] = MTX_CMD_MODE;
  buf[1] = mode_b;
  return mightex_send(m, buf, 2);
}

 char *mightex_serial_no(mightex_t *m) {
   return (char *)m->device_info.di.serial_no;
 }

 char *mightex_version(mightex_t *m) {
   return m->version;
 }
 

// t is in ms
int mightex_set_exptime(mightex_t *m, uint16_t t) {
  BYTE buf[3];
  uint16_t val = htons(t * 10);
  buf[0] = MTX_CMD_EXPTIME;
  memcpy(buf + 1, &val, sizeof(val));
  return mightex_send(m, buf, 3);
}

int mightex_get_buffer_count(mightex_t *m) {
  BYTE buf[3];
  int rc;
  buf[0] = MTX_CMD_BUFFEREDFRAMES;
  buf[1] = 0x00;
  buf[2] = 0x00;
  mightex_send(m, buf, 2);
  rc = mightex_receive(m, buf, 3);
  if (rc <= 0)
    return rc;
  return (int)buf[2];
}

int mightex_prepare_buffered_data(mightex_t *m, BYTE n) {
  BYTE buf[2];
  buf[0] = MTX_CMD_GETBUFFEREDDATA;
  buf[1] = n;
  return mightex_send(m, buf, 2);
}

int mightex_read_buffered_data(mightex_t *m, ccd_frames_t *frame) {
  return libusb_bulk_transfer(m->handle, MTX_EP_FRAME, frame->buf, sizeof(frame->frame), NULL, m->timeout);
}

mightex_t *mightex_new() {
  mightex_t *m = malloc(sizeof(mightex_t));
  int rc;
  ssize_t cnt, i = 0;
  libusb_device **devs;

  m->timeout = MTX_TIMEOUT;
  m->desc = malloc(sizeof(m->desc));

  rc = libusb_init(&m->ctx);
  if (rc < 0)
    return NULL;

  rc =
      libusb_set_option(m->ctx, LIBUSB_OPTION_LOG_LEVEL, LIBUSB_LOG_LEVEL_NONE);
  if (rc != LIBUSB_SUCCESS)
    fprintf(stderr, "Could not set log level (%s).\n", libusb_error_name(rc));

  cnt = libusb_get_device_list(m->ctx, &devs);
  if (cnt < 0) {
    fprintf(stderr, "No devices available.\n");
    libusb_exit(m->ctx);
    return NULL;
  }

  while ((m->dev = devs[i++]) != NULL) {
    rc = libusb_get_device_descriptor(m->dev, m->desc);
    if (rc < 0) {
      fprintf(stderr, "failed to get device descriptor (%s).",
              libusb_error_name(rc));
      libusb_exit(m->ctx);
      continue;
    }
    if (m->desc->idVendor == USB_IDVENDOR &&
        m->desc->idProduct == USB_IDPRODUCT) {
      device_info_t b;
      memset(&b, 0, sizeof(b));
      rc = libusb_open(m->dev, &m->handle);
      if (rc != LIBUSB_SUCCESS)
        printf("%d rc: %s\n", __LINE__, libusb_error_name(rc));
      rc = libusb_reset_device(m->handle);
      if (rc != LIBUSB_SUCCESS)
        printf("%d rc: %s\n", __LINE__, libusb_error_name(rc));
      rc = libusb_set_auto_detach_kernel_driver(m->handle, 1);
      if (rc != LIBUSB_ERROR_NOT_SUPPORTED)
        printf("%d rc: %s\n", __LINE__, libusb_error_name(rc));
      rc = libusb_get_string_descriptor_ascii(m->handle, m->desc->iManufacturer,
                                              m->manufacturer,
                                              sizeof(m->manufacturer));
      if (rc <= 0)
        printf("%d rc: %s\n", __LINE__, libusb_error_name(rc));
      rc = libusb_get_string_descriptor_ascii(m->handle, m->desc->iProduct,
                                              m->product, sizeof(m->product));
      if (rc <= 0)
        printf("%d rc: %s\n", __LINE__, libusb_error_name(rc));
      printf("Found device: %s - %s\n", m->manufacturer, m->product);
      rc = libusb_claim_interface(m->handle, 0);
      if (rc != LIBUSB_SUCCESS)
        printf("%d rc: %s\n", __LINE__, libusb_error_name(rc));

      mightex_get_version(m);
      printf("Version: %s\n", mightex_version(m));

      mightex_get_info(m);
      printf("SerialNo.: %s\n", mightex_serial_no(m));
      
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
      printf("%d rc: %s\n", __LINE__, libusb_error_name(rc));
    libusb_close(m->handle);
  }
  libusb_exit(m->ctx);
  free(m);
}

int main(void) {
  mightex_t *m = mightex_new();
  ccd_frames_t frame;
  int n;

  if (!m) {
    printf("Mightex not found!\n");
    mightex_close(m);
    exit(EXIT_FAILURE);
  }

  mightex_set_mode(m, MTX_NORMAL_MODE);
  mightex_set_exptime(m, 1);
  usleep(500000);
  n = mightex_get_buffer_count(m);
  printf("Frame count: %d\n", n);
  if (n > 0) {
    int i;
    uint16_t avg = 0;
    memset(&frame, 0, sizeof(frame));
    mightex_prepare_buffered_data(m, 1);
    mightex_read_buffered_data(m, &frame);

    for (i = 0; i < 13; i++) {
      avg += frame.frame.light_shield[i];
    }
    avg /= 13;

    for (i = 0; i < 3648; i++) {
      printf("%d %u\n", i, frame.frame.image_data[i] < avg ? 0 : frame.frame.image_data[i] - avg);
    }
  }

  mightex_close(m);

  return 0;
}
