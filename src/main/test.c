

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "../mightex1304.h"

int main(void) {
  mightex_t *m = mightex_new();
  int n;

  if (!m) {
    fprintf(stderr, "Mightex not found!\n");
    mightex_close(m);
    exit(EXIT_FAILURE);
  }

  if (mightex_set_mode(m, MTX_NORMAL_MODE) != MTX_OK) {
    fprintf(stderr, "Failed setting mode\n");
  }
  if (mightex_set_exptime(m, 0.1) != MTX_OK) {
    fprintf(stderr, "Failed setting esposure time\n");
  }

  while ((n = mightex_get_buffer_count(m)) <= 0) {
    usleep(10000);
  }
  fprintf(stderr, "Frame count: %d\n", n);
  int i;
  uint16_t *raw_data = mightex_raw_frame_p(m);
  uint16_t *data = mightex_frame_p(m);

  mightex_read_frame(m);
  fprintf(stderr, "Dark current level: %d\n", mightex_dark_mean(m));
  mightex_apply_filter(m, NULL);

  for (i = 0; i < mightex_pixel_count(m); i++) {
    printf("%d %u %u\n", i, raw_data[i], data[i]);
  }

  printf("Estimate: %f\n", mightex_apply_estimator(m, NULL));

  mightex_close(m);

  return 0;
}
