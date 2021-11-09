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
- documentation, under `doc/html`

## Wrappers

The folder `interfaces/wrappers` contains [SWIG](http://swig.org)-generated interface files for compiling binary extension libraries for Lua, Python, and Ruby.

Look at the source [README file](https://github.com/pbosetti/mightex/#readme) for how to quickly build and use the Python wrapper.

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

**Note**: for safety reasons, MacOS tags downloaded executables from unknown developers as potentially unsafe. This includes compiled libraries. In order to allow Python or Matlab to use the provided libraries, you need to "untaint" them befor using (first time only):

```sh
cd <path to the unzipped package>
xattr -dr com.apple.quarantine .
```

This command removes the extended attribute `com.apple.quarantine` recursively to the current folder and to every file and subfolder in it.

Now you can use the precompiled libraries as follows:

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
