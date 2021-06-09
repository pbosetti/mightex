#include <stdio.h>
#include <stdlib.h>
#ifdef _WIN32
#include <stdint.h>
#else
#include <unistd.h>
#endif // _WIN32
#include <math.h>
#include <libgen.h>
#include "../getopt.h"
#include "../mightex1304.h"

struct stats {
  double avg, std;
  uint16_t min, max;
};

static double mean(uint16_t *const d, uint16_t len) {
  uint16_t i;
  uint16_t sum = 0;
  for (i = 0; i < len; i++) {
    sum += d[i];
  }
  return (double)sum / len;
}

static double stdev(mightex_t *m, uint16_t *const data, uint16_t len,
                    void *ud) {
  uint16_t i;
  struct stats *s = (struct stats *)ud;
  s->avg = mean(data, len);
  s->min = data[0];
  s->max = data[0];
  for (i = 0; i < len; i++) {
    s->min = s->min < data[i] ? s->min: data[i];
    s->max = s->max > data[i] ? s->max: data[i];
    s->std += pow(data[i] - s->avg, 2);
  }
  s->std = sqrt(s->std / len - 1);
  return s->std;
}

int main(int argc, char *const argv[]) {
  int n, i;
  uint16_t *raw_data;
  uint16_t *data;
  int opt, nodata = 0;
  float exp = 0.1;
  struct stats stats;

  while ((opt = getopt(argc, argv, "e:n?h")) != -1) {
    switch (opt)
    {
    case 'e':
      exp = atof(optarg);
      break;
    case 'n':
      nodata = 1;
      break;
    case 'h':
    case '?':
      printf("%s - based on %s\n", basename((char *)argv[0]), mightex_sw_version());
      printf("Options:\
      \n\t-n:      print no data\
      \n\t-e<val>: set exposure time to val msec (min: 0.1)\
      ");
      return 0;
    default:
      break;
    }
  }

  mightex_t *m = mightex_new();
  if (!m) {
    fprintf(stderr,
            "No Mightex camera detected or unable to connect, exiting.\n");
    mightex_close(m);
    exit(EXIT_FAILURE);
  }

  // use custom estimator for calculating standard deviation of dark scene
  mightex_set_estimator(m, stdev);

  if (mightex_set_exptime(m, exp) != MTX_OK) {
    fprintf(stderr, "Failed setting esposure time\n");
  }
  fprintf(stderr, "Esposure time set to %.1f ms\n", exp);

  if (mightex_set_mode(m, MTX_NORMAL_MODE) != MTX_OK) {
    fprintf(stderr, "Failed setting mode\n");
  }

  // Setup data pointers
  raw_data = mightex_raw_frame_p(m);
  data = mightex_frame_p(m);

  // wait for a frame to be available
  while ((n = mightex_get_buffer_count(m)) <= 0) {
#ifdef _WIN32
    Sleep(10);
#else
    usleep(10000);
#endif
  }

  // read the frame
  mightex_read_frame(m);
  mightex_apply_filter(m, NULL);

  fprintf(stderr, "Dark current level: %d\n", mightex_dark_mean(m));
  mightex_apply_estimator(m, &stats);
  fprintf(stderr, "Mean value: %f\n", stats.avg);
  fprintf(stderr, "Std.dev.: %f\n", stats.std);
  fprintf(stderr, "Range: %d - %d\n", stats.min, stats.max);

  // print frame data
  if (! nodata) {
    for (i = 0; i < mightex_pixel_count(m); i++) {
      printf("%d %u %u\n", i, raw_data[i], data[i]);
    }
  }

  // close device
  mightex_close(m);

  return 0;
}
