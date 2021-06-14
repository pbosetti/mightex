# Mightex1304 library - binary version

Hello! this is a precompiled binary version of the [mightex library](https://github.com/pbosetti/mightex) for dealing with Mightex TCE-1304-U USB CCD line cameras.

If you are interested in other models, have a look at the source repo, for it shall be relatively easy to adapt the library to other similar Mightex CCD line cameras.

## Contents

This package contains:
- the precompiled library (both static and shared version) under `lib`
- library public headers, under `include`
- simple compiled programs, under `bin`
- interfaces for high-level programming languages (currently, Matlab only)
- C source examples about how to use the library, in `examples`

## Wrappers

The folder `interfaces/wrappers` contains [SWIG](http://swig.org)-generated interface files for compiling binary extension libraries for Lua, Python, and Ruby.

## How to compile the examples

### Linux

```sh
cd examples
gcc grab.c -I../include -L../lib ../lib/libusb-1.0.a ../lib/libmightex_static.a -pthread -o grab
```

If you prefer dynamic linking:
```sh
clang grab.c -I../include -L../lib -pthread -lmightex -pthread -o grab
```
then move `libmightex.so` in the executable directory, or change `LD_LIBRARY_PATH` suitably.

### MacOS

```sh
cd examples
clang grab.c -I../include -L../lib ../lib/libusb-1.0.a ../lib/libmightex_static.a -framework IOKit -framework CoreFoundation -o grab
```

If you prefer dynamic linking:
```sh
clang grab.c -I../include -L../lib -lmightex -pthread -o grab -rpath @executable_path/../lib
```

Note that the file `libmightex.dylib` is not signed, so you will have to enable it in System Preferences > Security and Privacy.

### Windows

```sh
cd examples
cl grab.c /I..\include /L..\lib ..\lib\libusb-1.0.lib ..\lib\mightex_static.lib /O grab
```

## Author

Paolo Bosetti, University of Trento.