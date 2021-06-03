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

#include <assert.h>
#include <libusb-1.0/libusb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define USB_IDVENDOR 0x04B4
#define USB_IDPRODUCT 0x0328
#define MTX_TIMEOUT 500
#define MTX_EP_CMD 0x01
#define MTX_EP_REPLY 0x81
#define MTX_EP_FRAME 0x82

#define STRING_LENGTH 14
typedef unsigned char BYTE;

typedef union {
  struct __attribute__((__packed__)) di {
    BYTE rc;
    BYTE len;
    BYTE ConfigRevision;
    BYTE ModuleNo[STRING_LENGTH];
    BYTE SerialNo[STRING_LENGTH];
    BYTE ManuafactureDate[STRING_LENGTH];
  } di;
  struct __attribute__((__packed__)) ver {
    BYTE rc;
    BYTE len;
    BYTE major, minor, rev;
  } version;
  BYTE buf[sizeof(struct di)];
} device_info_t;

typedef union {
  struct __attribute__((__packed__)) frame {
    uint16_t Dummy1[16];
    uint16_t LightShield[13];
    uint16_t Reserved[3];
    uint16_t ImageData[3648];
    uint16_t Dummy2[14];
    uint16_t Padding[138];
    uint16_t TimeStamp;
    uint16_t ExposureTime;
    uint16_t TriggerOccurred;
    uint16_t TriggerEventCount;
    uint16_t Padding2[4];
  } frame;
  BYTE buf[sizeof(struct frame)];
} tCCDFrames;

typedef struct {
  libusb_device *dev;
  libusb_device_handle *handle;
  libusb_context *ctx;
  struct libusb_device_descriptor *desc;
  unsigned char manufacturer[256];
  unsigned char product[256];
  unsigned int timeout;
} mightex_t;

static int mightex_send(mightex_t *m, BYTE *buf, int len) {
  int rc;
  rc = libusb_bulk_transfer(m->handle, MTX_EP_CMD, buf, len, NULL, m->timeout);
  if (rc != LIBUSB_SUCCESS)
    printf("Error on send: %s\n", libusb_error_name(rc));
  return (int)buf[0];
}

static int mightex_receive(mightex_t *m, BYTE *buf, int len) {
  int rc;
  rc =
      libusb_bulk_transfer(m->handle, MTX_EP_REPLY, buf, len, NULL, m->timeout);
  if (rc != LIBUSB_SUCCESS)
    printf("Error on receive: %s\n", libusb_error_name(rc));
  return (int)buf[0];
}

mightex_t *mightex_new() {
  mightex_t *m = malloc(sizeof(mightex_t));
  int rc, len;
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

      // Firmware version
      memset(&b, 0, sizeof(b));
      b.buf[0] = 0x01;
      b.buf[1] = 0x02;
      mightex_send(m, b.buf, 2);
      // rc = libusb_bulk_transfer(m->handle, 0x01, buf, 2, &len, 500);
      // if (rc != LIBUSB_SUCCESS)
      //   printf("%d rc: %s\n", __LINE__, libusb_error_name(rc));
      // printf("Buffer[%03d]: ", len);
      // for (i = 0; i < len; i++) {
      //   printf("%02x", buf[i]);
      // }
      // printf("\n");

      memset(&b, 0, sizeof(b));
      b.buf[0] = 0x01;
      b.buf[1] = 0x00;
      mightex_receive(m, b.buf, 5);
      // rc = libusb_bulk_transfer(m->handle, 0x81, buf, 5, &len, 500);
      // if (rc != LIBUSB_SUCCESS)
      //   printf("%d rc: %s\n", __LINE__, libusb_error_name(rc));
      // printf("Buffer[%03d]: ", len);
      // for (i = 0; i < len; i++) {
      //   printf("%02x", buf[i]);
      // }
      // printf("\n");
      printf("Version: %02d.%02d.%02d\n", b.version.major, b.version.minor,
             b.version.rev);

      // Device info
      memset(&b, 0, sizeof(b));
      b.buf[0] = 0x21;
      b.buf[1] = 0x00;
      mightex_send(m, b.buf, 2);
      // rc = libusb_bulk_transfer(m->handle, 0x01, buf, 2, &len, 500);
      // if (rc != LIBUSB_SUCCESS)
      //   printf("%d rc: %s\n", __LINE__, libusb_error_name(rc));
      // printf("Buffer[%03d]: ", len);
      // for (i = 0; i < len; i++) {
      //   printf("%02x", buf[i]);
      // }
      printf("\n");

      memset(&b, 0, sizeof(b));
      b.buf[0] = 0x01;
      mightex_receive(m, b.buf, sizeof(b.di));
      // rc = libusb_bulk_transfer(m->handle, 0x81, b.buf, sizeof(b.buf), &len,
      //                           500);
      // if (rc != LIBUSB_SUCCESS)
      //   printf("%d rc: %s\n", __LINE__, libusb_error_name(rc));
      printf("Buffer[%03zu]: ", sizeof(b.di));
      for (i = 0; i < sizeof(b.di); i++) {
        printf("%02x", b.buf[i]);
      }
      printf("\n");
      printf("SerialNo.: %s\n", b.di.SerialNo);

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

  if (!m) {
    printf("Mightex not found!\n");
    mightex_close(m);
    exit(EXIT_FAILURE);
  }

  mightex_close(m);

  return 0;
}
