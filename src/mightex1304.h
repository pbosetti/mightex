#ifndef MIGHTEX1304_h
#define MIGHTEX1304_h
/**
 * @file mightex1304.h
 * @author Paolo Bosetti (paolo.bosetti@unitn.it)
 * @brief Userland driver for Mightex TCE-1304-U line CCD camera
 * @date 2021-06-04
 * 
 * @copyright Copyright (c) 2021
 * 
 */
#include <stdlib.h>
#include "defines.h"

/**
 * @brief Number of standard pixels
 */
#define MTX_PIXELS 3648

/**
 * @brief Number of "light-shield" pixels
 * 
 * Those are pixels that are shielded from light: their output provides a 
 * measure of the dark current in the sensor. Their values shall be averaged
 * and subtracted from measure values
 * 
 * @see mightex_dark_mean and mightex_read_frame
 */
#define MTX_DARK_PIXELS 13

typedef unsigned char BYTE;

/**
 * @brief The two possible operating modes: continuous or triggered
 */
typedef enum { MTX_NORMAL_MODE = 0, MTX_TRIGGER_MODE = 1 } mtx_mode_t;

/**
 * @brief Standard exit values for library functions
 */
typedef enum { MTX_FAIL = 0, MTX_OK = 1 } mtx_result_t;

/**
 * @brief Opaque structure encapsulating the driver
 */
typedef struct mightex mightex_t;

/**
 * @brief Create a new Mightex object
 * 
 * @return mightex_t* 
 */
mightex_t *mightex_new();

/**
 * @brief Set exposure time, in milliseconds
 * 
 * @param m the Mightex object
 * @param t the exposure time, in ms
 * @return mtx_result_t 
 */
mtx_result_t mightex_set_exptime(mightex_t *m, float t);

/**
 * @brief Return the number of available frames
 * 
 * The internal buffer of the Mightex 1304 can hold a maximum of 4 frames,
 * so this function returns a value from 0 to 4. Negative values mean an
 * error in the underlying usb driver.
 * 
 * @param m the Mightex object
 * @return int 
 */
int mightex_get_buffer_count(mightex_t *m);

/**
 * @brief Read a frame from the camera buffer
 * 
 * Read a frame and store it internally. Frame data can be accessed with the 
 * proper accessors. In particular, the pixel values array is stored in the 
 * location returned by @ref mightex_frame_p. Timestamp and dark mean are also 
 * updated.
 * 
 * @param m the Mightex object
 * @return mtx_result_t 
 * @see mightex_frame_p, mightex_frame_timestamp, mightex_dark_mean
 */
mtx_result_t mightex_read_frame(mightex_t *m);


/**
 * @brief Close the object
 * 
 * Close the object connection and free all resources.
 * 
 * @param m the Mightex object
 */
void mightex_close(mightex_t *m);

/**
 * @brief Set the operating mode
 * 
 * @param m the Mightex object
 * @param mode the desired mode
 * @return mtx_result_t 
 * @see mtx_mode_t
 */
mtx_result_t mightex_set_mode(mightex_t *m, mtx_mode_t mode);

/** @name Accessors
 * Accessors to Mightex object parameters
 */
/**@{*/

/**
 * @brief The serial number of the connected device
 * 
 * This is useful whenever you have two or more cameras of the same model 
 * connected to the same host.
 * 
 * @param m 
 * @return char* a pointer to the serial number string (internally stored)
 */
char *mightex_serial_no(mightex_t *m);

/**
 * @brief The firmware version of the connected device
 * 
 * @param m 
 * @return char* a pointer to the firmware version string (internally stored)
 */
char *mightex_version(mightex_t *m);

/**
 * @brief Return the pointer to the image storage area
 * 
 * The last frame, as collected with @ref mightex_read_frame, is stored as an
 * array of `uint16_t` in the location pointed by the returned pointer.
 * 
 * @param m 
 * @return uint16_t* An array of @ref MTX_PIXELS elements
 */
uint16_t *mightex_frame_p(mightex_t *m);

/**
 * @brief The timestamp of the last grabbed frame
 * 
 * @param m 
 * @return uint16_t Timestamp of the last grabbed frame
 * @note The values **are not** compensated for the dark current average!
 */
uint16_t mightex_frame_timestamp(mightex_t *m);

/**
 * @brief The mean of the shielded pixels
 * 
 * This returns the averaged value of the @ref MTX_DARK_PIXELS. This value
 * gives an estimate of the sensor dark current.
 * 
 * @param m 
 * @return uint16_t The average dark current value
 */
uint16_t mightex_dark_mean(mightex_t *m);

/**
 * @brief Return the number of pixels (@ref MTX_PIXELS)
 * 
 * @param m 
 * @return uint16_t 
 */
uint16_t mightex_pixel_count(mightex_t *m);

/**
 * @brief Return the number of shielded pixels (@ref MTX_DARK_PIXELS)
 * 
 * @param m 
 * @return uint16_t 
 */
uint16_t mightex_dark_pixel_count(mightex_t *m);
/**@}*/
#endif