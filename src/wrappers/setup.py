#!/usr/bin/env python3
# run as setup.py build_ext -fi to generate binary ext
from distutils.core import setup, Extension
import platform

module1 = Extension('_mightex', 
  sources=["mightex_py.cpp"],
  include_dirs=["../../include"],
)

if(platform.system() == "Darwin"):
  module1.extra_link_args=["../../lib/libmightex_static.a", "../../lib/libusb-1.0.a", "-framework", "IOKit", "-framework", "CoreFoundation"]
elif(platform.system() == "Linux"):
  module1.extra_link_args=["../../lib/libmightex_static.a", "../../lib/libusb-1.0.a"]
elif(platform.system() == "Windows"):
  module1.extra_link_args=["../../lib/libmightex_static.lib", "../../lib/libusb-1.0.lib"]

setup(name="Mightex",
  version = "1.0",
  description = "Mightex module",
  ext_modules = [module1],
)