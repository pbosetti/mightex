/**
 * @file mightex.hh
 * @author Paolo Bosetti (paolo.bosetti@unitn.it)
 * @brief C++ interface to the `mightex` driver library
 * @date 2021-06-16
 * 
 * Provides a class wrapping the underlying driver if you prefer using with
 * C++ semantics.
 * 
 * @copyright Copyright (c) 2021
 * 
 */

//   ______        _____ ____       _          __  __ 
//  / ___\ \      / /_ _/ ___|  ___| |_ _   _ / _|/ _|
//  \___ \\ \ /\ / / | | |  _  / __| __| | | | |_| |_ 
//   ___) |\ V  V /  | | |_| | \__ \ |_| |_| |  _|  _|
//  |____/  \_/\_/  |___\____| |___/\__|\__,_|_| |_|  
                                                   
#ifdef SWIG
// Generate libraries for scripting languages with SWIG
// e.g., for Lua, generate with swig -lua -c++ -o mightex_lua.cpp mightex.hpp
// then build with:
// clang++ mightex_lua.cpp ../lib/libusb-1.0.a ../lib/libmightex_static.a -I /usr/local/opt/lua@5.3/include/lua -L /usr/local/opt/lua@5.3/lib/ -llua -shared -framework IOKit -framework CoreFoundation -omightex.so 
%module mightex
%{
#include "mightex.hh"
%}
%include "mightex1304.h"
%include "std_string.i"
%include "std_vector.i"
namespace std {
  %template(VecInt) vector<int>;
};
#endif


//    ____            
//   / ___| _     _   
//  | |   _| |_ _| |_ 
//  | |__|_   _|_   _|
//   \____||_|   |_|  
                   
#include <string>
#include <vector>

#include "mightex1304.h"


/**
 * @brief Version of the underlying `mightex` library
 * 
 * @return std::string 
 */
std::string version() { return mightex_sw_version(); }

/**
 * @brief Class wrapping the underlying `mightex` library
 * 
 * This class also provides a straight conversion into binary modules for
 * scripting languages via [SWIG](https://swig.org). You can generate a wrapper
 * for your language of choice with the command:
 * 
 * ```sh
 * $ swig -python -c++ -o mightex_py.cpp mightex.hh
 * ```
 * 
 * then, after compiling the generated `mightex_py.cpp`, in Python you can 
 * simply do:
 * 
 * ```python
 * from mightex import Mightex1304
 * m = Mightex1304()
 * m.set_exptime(0.1)
 * m.read_frame()
 * m.apply_filter()
 * f = m.frame()
 * r = m.raw_frame()
 * ```
 * 
 * and so on, following the same class methods defined in C++. The same works 
 * for other languages as Ruby and Lua.
 */
class Mightex1304 {
private:
  mightex_t *m;
  std::string _serial;
  std::string _version;
  uint16_t *_frame_p, *_raw_frame_p;

public:
  /**
   * @brief Construct a new Mightex1304 object and open device connection
   * 
   */
  Mightex1304() {
    m = mightex_new();
    _frame_p = mightex_frame_p(m);
    _raw_frame_p = mightex_raw_frame_p(m);
    _serial = mightex_serial_no(m);
    _version = mightex_version(m);
  }

  /**
   * @brief Close device connection and destroy the Mightex1304 object
   * 
   */
  ~Mightex1304() { mightex_close(m); }

  /**
   * @brief Serial number of connected device
   * 
   * @return std::string 
   */
  std::string serial_no() { return _serial; }

  /**
   * @brief Firmware version of the connected device
   * 
   * @return std::string 
   */
  std::string version() { return _version; }

  /**
   * @brief Return the number of pixels in the sensor
   * 
   * @return uint16_t 
   */
  uint16_t pixel_count() { return MTX_PIXELS; }

  /**
   * @brief Return the number of shielded pixels on the sensor
   * 
   * @return int 
   */
  int dark_pixel_count() { return MTX_DARK_PIXELS; }

  /**
   * @brief Set the exposure time
   * 
   * Exposure time can be set in increments of 0.1 ms
   * 
   * @param t Exposure time in ms
   * @return mtx_result_t 
   */
  mtx_result_t set_exptime(float t) {
    return mightex_set_exptime(m, t);
  }

  /**
   * @brief Set the mode object
   * 
   * @param mode  either MTX_NORMAL_MODE or MTX_TRIGGER_MODE
   * @return mtx_result_t 
   */
  mtx_result_t set_mode(mtx_mode_t mode) {
    return mightex_set_mode(m, mode);
  }

  /**
   * @brief Read and store a frame
   * 
   * @return mtx_result_t 
   */
  mtx_result_t read_frame() { return mightex_read_frame(m); }

  /**
   * @brief The mean of the shielded pixels
   * 
   * This returns the averaged value of the @ref MTX_DARK_PIXELS. This value
   * gives an estimate of the sensor dark current.
   * 
   * @return int 
   */
  int dark_mean() { return (int)mightex_dark_mean(m); }

  /**
   * @brief Return the timestamp of the last read frame
   * 
   * @return int 
   * @warning Overflows every 2^16
   */
  unsigned int timestamp() { return (unsigned int)mightex_frame_timestamp(m); }

  /**
   * @brief Return a vector containing the values of the last frame
   * 
   * Frame values could be possibly filtered, if @ref Mightex1304.apply_filter has
   * been called previously.
   * 
   * @return std::vector<int> 
   */
  std::vector<int> frame() {
    std::vector<int> data(_frame_p, _frame_p + mightex_pixel_count(m));
    return data;
  }

  /**
   * @brief Return a vector containing unfiltered values of the last frame
   * 
   * @return std::vector<int> 
   */
  std::vector<int> raw_frame() {
    std::vector<int> data(_raw_frame_p, _raw_frame_p + mightex_pixel_count(m));
    return data;
  }

  /**
   * @brief Apply the current filter
   * 
   */
  void apply_filter() { mightex_apply_filter(m, NULL); }

  /**
   * @brief Apply the current estimator
   * 
   * @return double 
   */
  double apply_estimator() { return mightex_apply_estimator(m, NULL); }

  /**
   * @brief Write a value to a GPIO register
   * 
   * @param reg Register number (0--3)
   * @param val GPIO register level (1 or 0)
   */
  void gpio_write(int reg, int val) {
    mightex_gpio_write(m, (BYTE)reg, (BYTE)val);
  }

  /**
   * @brief Read a GPIO register level
   * 
   * @param reg Register number (0--3)
   * @return int 
   */
  int gpio_read(int reg) {
    return (int)mightex_gpio_read(m, (BYTE)reg);
  }

#ifndef SWIG
  /**
   * @name Not exposed to SWIG
   * @brief Methods that are not exposed to SWIG
   * 
   * These methods are not exposed to SWIG and thus are not available in 
   * scripting languages extensions.
   * 
   */
  /**@{*/

  /**
   * @brief Apply the current filter passing user data field
   * 
   * @param ud 
   * @note This method is **not exposed** via SWIG.
   */
  void apply_filter(void *ud) { mightex_apply_filter(m, ud); }

  /**
   * @brief Set the filter
   * 
   * Set a custom filtering function (it can be NULL). The function may 
   * get a nullable pointer to user data.
   * 
   * @param f 
   * @note This method is **not exposed** via SWIG.
   */
  void set_filter(mightex_filter_t *f) { mightex_set_filter(m, f); }

  /**
   * @brief Reset the filter to the defaiult
   * 
   * Defailt filter removes the bias estimated as average of shielded pixels.
   * 
   * @note This method is **not exposed** via SWIG.
   */
  void reset_filter() { mightex_reset_filter(m); }

  /**
   * @brief Apply the current estimator passing user data
   * 
   * @return double 
   * @note This method is **not exposed** via SWIG.
   */
  double apply_estimator(void *ud) { return mightex_apply_estimator(m, ud); }

  /**
   * @brief Set the estimator object
   * 
   * The estimator is a custom function that calculates a scalar estimate
   * from the vector of pixel values. That function can take a nullable
   * pointer to user data.
   * 
   * @param e 
   * @note This method is **not exposed** via SWIG.
   */
  void set_estimator(mightex_estimator_t *e) { mightex_set_estimator(m, e); }

  /**
   * @brief Reset the estimator function
   * 
   * By default, it calculates the centroid of the current "image".
   * 
   * @note This method is **not exposed** via SWIG.
   */
  void reset_estimator() { mightex_reset_estimator(m); }
#endif
/**@}*/
};
