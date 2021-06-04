

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "../mightex1304.h"

int main(void) {
  mightex_t *m = mightex_new();
  int n;

  if (!m) {
    printf("Mightex not found!\n");
    mightex_close(m);
    exit(EXIT_FAILURE);
  }

  if (mightex_set_mode(m, MTX_NORMAL_MODE) != MTX_OK) {
    printf("Failed setting mode\n");
  }
  if (mightex_set_exptime(m, 0.1) != MTX_OK) {
    printf("Failed setting esposure time\n");
  }
  usleep(1000000);
  n = mightex_get_buffer_count(m);
  printf("Frame count: %d\n", n);
  if (n > 0) {
    int i;
    uint16_t *data = mightex_frame_p(m);

    mightex_read_frame(m);
    for (i = 0; i < 10; i++) {
      printf("%d %u %u\n", i, data[i], data[i] < mightex_dark_mean(m) ? 0 : data[i] - mightex_dark_mean(m));
    }
  }

  mightex_close(m);

  return 0;
}
