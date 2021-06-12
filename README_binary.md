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

## How to compile the examples

### Linux

```sh
cd examples
gcc grab.c -I../include -L../lib ../lib/libusb-1.0.a ../lib/libmightex_static.a -pthread -o grab
```

### MacOS

```sh
cd examples
clang grab.c -I../include -L../lib ../lib/libusb-1.0.a ../lib/libmightex_static.a -pthread -framework IOKit -framework CoreFoundation -o grab
```

### Windows

```sh
cd examples
cl grab.c /I..\include /L..\lib ..\lib\libusb-1.0.lib ..\lib\mightex_static.lib /O grab
```

## Author

Paolo Bosetti, University of Trento.