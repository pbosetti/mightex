# Mightex1304
[![CMake](https://github.com/pbosetti/mightex/actions/workflows/cmake.yml/badge.svg)](https://github.com/pbosetti/mightex/actions/workflows/cmake.yml)[![Open in Visual Studio Code](https://open.vscode.dev/badges/open-in-vscode.svg)](https://open.vscode.dev/pbosetti/mightex)


This is a userland driver for the [Mightex TCE-1304-u CCD line cameras](https://www.mightexsystems.com/product/usb2-0-3648-pixel-16-bit-ccd-line-camera-with-external-trigger/). It is based on [libusb](https://libusb.info).

**Note:** at the moment it is all very preliminary. Do not expect a finished product, although it can prove helpful in understanding how to communicate with the camera using the public protocol provided by Mightex. The libraries (shared and static) produced by this project, though, are now functional enough to be used directly in your projects.

## What it provides

A simple interface to a Mightex CCD line camera, as simple as possible. Look at the `src/main/test.c` program to see how simple it can be ;)

The project compiles a static and a dynamic library: the documentation for the library can be build with Doxygen (`make doc`). The dynamic library embeds the `libusb` library, so there are no dependencies. If you rather want to use the static library, then you also need to link in your program the `vendor/lib/libusb-1.0.a` static library.

The library is designed to make the life easier to you if you want to use it within an interpreted language via FFI. As an example, the `matlab` folder contains a Matlab class that wraps the library. I plan to add FFI examples for Python and Ruby too.

The project also enables quick creation of binary language extensions for scripting languages via [SWIG](http://swig.org). See later on.

## Supported platforms

Linux, OS X, and Windows. For the latter, see the dedicated section in this document.

## Build

### Linux and OS X

Cmake style:

```sh
cmake -Bbuild -H. -DCMAKE_BUILD_TYPE=Release
make -Cbuild -j4 install
make -Cbuild doc
```

and you will find the compiled files under `products_host` and Doxygen documentation under `doc`.

The project is ready for an easy, Docker-based cross-compilation. See README-xcomp.md for details.

### Windows

If you want to build the project on Windows, things are a tad more complicated. For starters you need to install:

1. Visual Studio 2019 (Community edition is fine), with Windows C++ SDK.
2. Git
3. CMake
4. **Most importantly** do not use the driver provided by Mightex, it won't work. Instead, you have to use the WinUSB driver: the easiest way to install it is by using the [Zadig tool](https://zadig.akeo.ie/).

Then you shall be able to do:

```sh
cmake -Bbuild -S. -DBUILD_DOC=no -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4 -t install
```

and you'll find all the goodies in `products_host`.

If you only want to **use** the library on Windows, perhaps via Matlab, then you 
need to download a release version of the library (in zip format) and also be sure to have installed the [Microsoft Visual C++ Redistributable package](https://visualstudio.microsoft.com/downloads/). Also, remember to use the proper WinUSB driver as per the point 4. above.

At the moment, **the Docker-based cross compilation is not enabled on Windows**.

## SWIG support

[SWIG](http://swig.org) is a commandline utility that simplifies the creation of C/C++ wrappers to be compiled into binary libraries for many common scripting languages.

This project enables that with a special C++ header, which can also be parsed by SWIG: `src/mightex.hh`.

This file also plays as a header only C++ interface to the Mightex1304 library, so if you want to use the library in a C++ fashion, just `#include "mightex.hh"` and properly link to the libraries, and you are set.

The file also contains some conditional code that enables SWIG to parse it and generate the proper code. Let's see how it works for lua:

```sh
$ cd products_host/include
$ swig -lua -c++ -o mightex_lua.cpp mightex.hh
```

this generates the source file `mightex_lua.cpp`, which can be compiled into a shared object run-time loadable by the lua interpreter:

```sh
$ clang++ mightex_lua.cpp -shared -I /usr/local/opt/lua@5.3/include/lua -L /usr/local/opt/lua@5.3/lib/ -llua -framework IOKit -framework CoreFoundation ../lib/libusb-1.0.a ../lib/libmightex_static.a -omightex.so
# mightex.so is created...
$ lua -l mightex # this starts the interpreter loading the mightex library
Lua 5.3.5  Copyright (C) 1994-2018 Lua.org, PUC-Rio
> mightex.version()
Mightex1304 v.0.2.2-9-g8337fbc for x86_64-apple-darwin19.6.0, Release build.
> m = mightex.Mightex1304()
> Found device: Mightex - USB-TCD1304-1
> Version: 1.3.0
> SerialNo.: 13-201114-002
> m:read_frame()
1.0
> v = m:frame()
> v[0]
2039.0
```

which shall be clear considering that: `mightex` is the name of the module and `Mightex1304` is the name of the class, so `mightex.version()` is a module function and `m:read_frame()` is an object method.

A Python interface can be rather analogously generated and used with `swig -python -c++ -o mightex_lua.cpp mightex.hpp`.

The binary release already contains wrappers for Lua, Python, and Ruby. In particular, from the binary release you can quickly run the Python library like this:

```sh
$ cd interfaces/wrappers
$ python3 setup.py build_ext -fi
$ python3
Python 3.9.1 (default, Jan  8 2021, 17:17:43) 
[Clang 12.0.0 (clang-1200.0.32.28)] on darwin
Type "help", "copyright", "credits" or "license" for more information.
>>> import mightex
>>> m = mightex.Mightex1304()
> Found device: Mightex - USB-TCD1304-1
> Version: 1.3.0
> SerialNo.: 13-201114-002
>>> m.set_exptime(0.1)
1
>>> m.read_frame()
1
>>> v = m.frame()
>>> v[1]
1955
```

**NOTE**: On linux and MacOS, the script `seyup.py` builds a statically linked shared object, so you can move the extension around by just copying the files `mightex.py` and the shared object generated with extension `.pyd`. On Windows, on the other hand, the script build a shared object that is dynamically linked to the mightex dynamic library, which is copied locally for convenience; so if you want to move it around, you have to copy *three* files: `mightex.py`, the dynamic library `libmightex.dll`, and the shared object `.pyd` generated by `setup.py`.

## Author

Paolo Bosetti, University of Trento.
