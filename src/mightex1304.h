#ifndef MIGHTEX1304_h
#define MIGHTEX1304_h

#include <stdlib.h>

#define MTX_PIXELS 3648
#define MTX_BLACK_PIXELS 13

typedef unsigned char BYTE;

typedef enum { MTX_NORMAL_MODE = 0, MTX_TRIGGER_MODE = 1 } mtx_mode_t;

typedef enum { MTX_FAIL = 0, MTX_OK = 1 } mtx_result_t;

typedef struct mightex mightex_t;

mtx_result_t mightex_get_version(mightex_t *m);

mtx_result_t mightex_get_info(mightex_t *m);

mtx_result_t mightex_set_mode(mightex_t *m, mtx_mode_t mode);

char *mightex_serial_no(mightex_t *m);

char *mightex_version(mightex_t *m);

uint16_t *mightex_frame_p(mightex_t *m);

uint16_t mightex_frame_timestamp(mightex_t *m);

uint16_t mightex_dark_mean(mightex_t *m);

// t is in ms
mtx_result_t mightex_set_exptime(mightex_t *m, float t);

int mightex_get_buffer_count(mightex_t *m);

mtx_result_t mightex_prepare_buffered_data(mightex_t *m, BYTE n);

mtx_result_t mightex_read_frame(mightex_t *m);

mightex_t *mightex_new();

void mightex_close(mightex_t *m);

#endif