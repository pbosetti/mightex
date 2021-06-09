# Mightex1304

This is a userland driver for the [Mightex TCE-1304-u CCD line cameras](https://www.mightexsystems.com/product/usb2-0-3648-pixel-16-bit-ccd-line-camera-with-external-trigger/). It is based on [libusb](https://libusb.info).

**Note:** at the moment it is all very preliminary. Do not expect a finished product, although it can prove helpful in understanding how to communicate with the camera using the public protocol provided by Mightex. The libraries (shared and static) produced by this project, though, are now functional enough to be used directly in your projects.

## What it provides

A simple interface to a Mightex CCD line camera, as simple as possible. Look at the `src/main/test.c` program to see how simple it can be ;)

The project compiles a static and a dynamic library: the documentation for the library can be build with Doxygen (`make doc`). The dynamic library embeds the `libusb` library, so there are no dependencies. If you rather want to use the static library, then you also need to link in your program the `vendor/lib/libusb-1.0.a` static library.

The library is designed to make the life easier to you if you want to use it within an interpreted language via FFI. As an example, the `matlab` folder contains a Matlab class that wraps the library. I plan to add FFI examples for Python and Ruby too.

## Supported platforms

At the moment, Linux and OS X. Given that `libusb` is also available on Windows, I plan to add that platform to the list.

## Build

### Linux and OS X

Cmake style:

```sh
cmake -Bbuild -H. -DCMAKE_BUILD_TYPE=Release
make -Cbuild -j4 install
make -Cbuild doc
```

and you will find the compiled files under `products_host` and Doxygen documentation under `doc`.

The project is ready for an easy cross-compilation. See README-xcomp.md for details.

### Windows

On Windows, things are a tad more complicated. For starters you need to install:

1. Visual Studio 2019 (Community edition is fine), with Windows C++ SDK.
2. Git
3. CMake
4. **Most importantly** do not use the driver provided by Mightex, it won't work. Instead, you have to use the WinUSB driver: the easiest way to install it is by using the [Zadig tool](https://zadig.akeo.ie/).

Then you shall be able to do:

```sh
cmake -Bbuild -S. -DBUILD_DOC=no -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4 -t install
```

and you'll find all the goodies in `products_host`
