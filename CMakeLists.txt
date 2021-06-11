cmake_minimum_required(VERSION 3.16)

#   _____          _ _           _                        
#  |  __ \        | (_)         (_)                       
#  | |__) | __ ___| |_ _ __ ___  _ _ __   __ _ _ __ _   _ 
#  |  ___/ '__/ _ \ | | '_ ` _ \| | '_ \ / _` | '__| | | |
#  | |   | | |  __/ | | | | | | | | | | | (_| | |  | |_| |
#  |_|   |_|  \___|_|_|_| |_| |_|_|_| |_|\__,_|_|   \__, |
#                                                    __/ |
#                                                   |___/ 
# GIT versioning - It requires at leas one git tag e.g. 0.0.1
# create it with: git tag -am " " 0.0.1
execute_process(
  COMMAND git describe --long --dirty=§
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  OUTPUT_VARIABLE GIT_COMMIT_HASH
  OUTPUT_STRIP_TRAILING_WHITESPACE
)
execute_process(
  COMMAND git describe --abbrev=0 --tags
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  OUTPUT_VARIABLE GIT_VERSION_TAG
  OUTPUT_STRIP_TRAILING_WHITESPACE
)
project(Mightex1304 VERSION ${GIT_VERSION_TAG} LANGUAGES C)
    
# Override build type (Debug or Release)
# set(CMAKE_BUILD_TYPE Debug)
# set(CMAKE_BUILD_TYPE Release)
message(STATUS "CMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}")

if(NOT WIN32)
  # Libusb
  set(LIBUSB_VERSION 1.0.24)
  if (CMAKE_BUILD_TYPE STREQUAL "Release")
    set(LIBUSB_NO_DEBUG --disable-log)
  endif()
  include(ExternalProject)
  ExternalProject_Add(libusb_prj
    URL https://github.com/libusb/libusb/releases/download/v${LIBUSB_VERSION}/libusb-${LIBUSB_VERSION}.tar.bz2
    PREFIX ${CMAKE_SOURCE_DIR}/vendor
    BUILD_IN_SOURCE TRUE
    CONFIGURE_COMMAND ./configure --prefix=${CMAKE_SOURCE_DIR}/vendor --enable-udev=no --with-pic -enable-shared=no ${LIBUSB_NO_DEBUG}
    BUILD_COMMAND make
    INSTALL_COMMAND make install 
  )
  ExternalProject_Get_Property(libusb_prj INSTALL_DIR)
  message(STATUS "Installing libusb in ${INSTALL_DIR}")
  add_library(libusb STATIC IMPORTED)
  set_property(TARGET libusb PROPERTY IMPORTED_LOCATION ${INSTALL_DIR}/lib/libusb-1.0.a)
endif()

#    _____                           _____                      
#   / ____|                         / ____|                     
#  | |     _ __ ___  ___ ___ ______| |     ___  _ __ ___  _ __  
#  | |    | '__/ _ \/ __/ __|______| |    / _ \| '_ ` _ \| '_ \ 
#  | |____| | | (_) \__ \__ \      | |___| (_) | | | | | | |_) |
#   \_____|_|  \___/|___/___/       \_____\___/|_| |_| |_| .__/ 
#                                                        | |    
#                                                        |_|    
# Find out target platform
if (NOT WIN32)
  execute_process(COMMAND ${CMAKE_C_COMPILER} -dumpmachine OUTPUT_VARIABLE TARGET_PLATFORM) #put into TARGET_PLATFORM result of $ gcc -dumpmachine
  string(REPLACE "\n" "" TARGET_PLATFORM ${TARGET_PLATFORM})
  string(REGEX MATCH mipsel-openwrt-linux-musl IS_MIPSEL ${TARGET_PLATFORM})
  string(REGEX MATCH armv7-unknown-linux-gnueabi IS_ARM7 ${TARGET_PLATFORM})
  string(REGEX MATCH arm-linux-gnueabihf IS_ARM6 ${TARGET_PLATFORM})
  string(REGEX MATCH arm-cortexa8_neon-linux-gnueabihf IS_ARM7A ${TARGET_PLATFORM})
  string(REGEX MATCH aarch64-unknown-linux-gnueabi IS_ARM64 ${TARGET_PLATFORM})
  if (IS_ARM6 OR IS_ARM7 OR IS_ARM7A OR IS_ARM64)
    set(IS_ARM TRUE)
  endif()

  # SETUP PATHS AND VARIABLES
  if(IS_MIPSEL)
    message(STATUS "Cross-building for MIPSEL (cross_root: ${cross_root})")
    set(LUA_LIB lua)
    add_definitions(-D_OPENWRT_LINUX=1)
    include_directories(${cross_root}/include)
    link_directories(${cross_root}/lib)
  elseif(IS_ARM)
    message(STATUS "Cross-building for ARM (cross_root: ${cross_root})")
    set(LINUX TRUE)
    set(LUA_LIB lua)
    include_directories(${cross_root}/include ${cross_root}/include/libusb-1.0)
    link_directories(${cross_root}/lib)
    add_definitions(-D__ARM7__)
  elseif(UNIX AND NOT APPLE)
    message(STATUS "Building on Linux")
    set(LINUX TRUE)
    set(NATIVE TRUE)
    set(LUA_LIB lua5.3)
    include_directories(/usr/include/${LUA_LIB} /usr/local/include ${CMAKE_SOURCE_DIR}/vendor/include)
    link_directories(/usr/lib/x86_64-linux-gnu ${CMAKE_SOURCE_DIR}/vendor/lib)
  elseif(APPLE)
    message(STATUS "Building on Apple OS X")
    set(LUA_LIB lua)
    set(NATIVE TRUE)
    include_directories(/usr/local/include /usr/local/include/lua ${CMAKE_SOURCE_DIR}/vendor/include)
    link_directories(${CMAKE_CURRENT_LIST_DIR}/vendor/lib)
    set(FRAMEWORKS "-framework CoreFoundation -framework IOKit")
  endif()

  # COMPILE OPTIONS
  add_compile_options(-std=gnu11 -fPIC -D_GNU_SOURCE)
  if(CMAKE_BUILD_TYPE MATCHES "Debug")
    message(STATUS "Debug mode, enabling all warnings")
    add_compile_options(-Wall -Wno-comment)
  endif()
  set(LIBUSB_NAME libusb)
else() #WIN32
  include_directories(${CMAKE_SOURCE_DIR}/win/include)
  link_directories(${CMAKE_SOURCE_DIR}/win/lib)
  set(LIBUSB_NAME libusb-1.0.lib)
  set(TARGET_PLATFORM "Windows")
  set(NATIVE TRUE)
endif()
#   _____           _           _     _____       _        _ _     
#  |  __ \         (_)         | |   |  __ \     | |      (_) |    
#  | |__) | __ ___  _  ___  ___| |_  | |  | | ___| |_ __ _ _| |___ 
#  |  ___/ '__/ _ \| |/ _ \/ __| __| | |  | |/ _ \ __/ _` | | / __|
#  | |   | | | (_) | |  __/ (__| |_  | |__| |  __/ || (_| | | \__ \
#  |_|   |_|  \___/| |\___|\___|\__| |_____/ \___|\__\__,_|_|_|___/
#                 _/ |                                             
#                |__/                                              
# FILES
# expect all sources in /src, except mains
set(SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}/src)
set(VENDOR ${CMAKE_CURRENT_LIST_DIR}/vendor)
file(GLOB LIB_SOURCES "${SOURCE_DIR}/*.c")
file(GLOB HEADERS "${SOURCE_DIR}/*.h")
# generate defines.h, which also contains version numbers matching git tags
configure_file(
  ${SOURCE_DIR}/defines.h.in
  ${SOURCE_DIR}/defines.h
)

# libs
include(CheckLibraryExists)
check_library_exists(m sqrt "" HAVE_LIB_M)
if(HAVE_LIB_M)
  message(STATUS "Including libm")
  list(APPEND EXTRA_LIBS m)
else()
  message(STATUS "libm not needed")
endif()

include(FindThreads)
if(Threads_FOUND)
  if("${CMAKE_THREAD_LIBS_INIT}" STREQUAL "")
    message(STATUS "Threads library don't need link flag")
  else()
    message(STATUS "Found Threads library, linking with "${CMAKE_THREAD_LIBS_INIT})
  endif()
else()
  message(ERROR "Thread library not found!")
endif()

# TARGETS
add_library(mightex_static STATIC ${LIB_SOURCES})
target_link_libraries(mightex_static ${LIBUSB_NAME} ${FRAMEWORKS} ${CMAKE_THREAD_LIBS_INIT})

add_library(mightex_shared SHARED ${LIB_SOURCES})
target_link_libraries(mightex_shared ${LIBUSB_NAME} ${FRAMEWORKS})
set_target_properties(mightex_shared PROPERTIES PREFIX "lib" OUTPUT_NAME "mightex")
set_target_properties(mightex_shared PROPERTIES PUBLIC_HEADER "${HEADERS}")

add_executable(grab ${SOURCE_DIR}/main/grab.c)
target_link_libraries(grab mightex_static ${EXTRA_LIBS})
  
add_executable(listusb ${SOURCE_DIR}/main/listusb.c)
target_link_libraries(listusb ${LIBUSB_NAME} ${FRAMEWORKS} ${CMAKE_THREAD_LIBS_INIT})
  
if (NOT WIN32)
  add_dependencies(mightex_static libusb libusb_prj)
  add_dependencies(mightex_shared libusb libusb_prj)
  add_dependencies(listusb libusb libusb_prj)
else()  
  set_target_properties(grab PROPERTIES LINK_FLAGS "/NODEFAULTLIB:LIBCMT")
  set_target_properties(listusb PROPERTIES LINK_FLAGS "/NODEFAULTLIB:LIBCMT")
  set_target_properties(mightex_shared PROPERTIES LINK_FLAGS "/NODEFAULTLIB:LIBCMT")
endif()

list(APPEND TARGETS_LIST
  grab listusb
  mightex_static mightex_shared
)


#   _____           _        _ _ 
#  |_   _|         | |      | | |
#    | |  _ __  ___| |_ __ _| | |
#    | | | '_ \/ __| __/ _` | | |
#   _| |_| | | \__ \ || (_| | | |
#  |_____|_| |_|___/\__\__,_|_|_|
if(NATIVE)
  set(CMAKE_INSTALL_PREFIX ${CMAKE_CURRENT_LIST_DIR}/products_host)
else()
  set(CMAKE_INSTALL_PREFIX ${CMAKE_CURRENT_LIST_DIR}/products)
endif()
install(TARGETS ${TARGETS_LIST}
  RUNTIME DESTINATION bin
  LIBRARY DESTINATION lib
  ARCHIVE DESTINATION lib
  PUBLIC_HEADER DESTINATION include
)
install(DIRECTORY ${CMAKE_SOURCE_DIR}/matlab
  DESTINATION interfaces
  PATTERN "*.asv" EXCLUDE
  PATTERN "*.pdf" EXCLUDE
)
if(BUILD_DOC)
  install(DIRECTORY ${CMAKE_SOURCE_DIR}/doc
    DESTINATION doc
  )
endif()

#   ____                 
#  |  _ \  ___   ___ ___ 
#  | | | |/ _ \ / __/ __|
#  | |_| | (_) | (__\__ \
#  |____/ \___/ \___|___/
option(BUILD_DOC "Build documentation" OFF)         
find_package(Doxygen)
if (BUILD_DOC AND DOXYGEN_FOUND)
  set(DOXYGEN_IN ${CMAKE_CURRENT_LIST_DIR}/Doxyfile.conf)
  set(DOXYGEN_OUT ${CMAKE_CURRENT_LIST_DIR}/doc/Doxyfile)

  # request to configure the file
  configure_file(${DOXYGEN_IN} ${DOXYGEN_OUT} @ONLY)
  message(STATUS "Doxygen documentation can be build with `make doc`")
  add_custom_target(doc
    COMMAND ${DOXYGEN_EXECUTABLE} ${DOXYGEN_OUT}
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Generating API documentation with Doxygen"
    VERBATIM
  )
elseif (BUILD_DOC AND NOT DOXYGEN_FOUND)
  message(ERROR "Doxygen need to be installed to generate the doxygen documentation")
endif (BUILD_DOC AND DOXYGEN_FOUND)

#   _____           _               _             
#  |  __ \         | |             (_)            
#  | |__) |_ _  ___| | ____ _  __ _ _ _ __   __ _ 
#  |  ___/ _` |/ __| |/ / _` |/ _` | | '_ \ / _` |
#  | |  | (_| | (__|   < (_| | (_| | | | | | (_| |
#  |_|   \__,_|\___|_|\_\__,_|\__, |_|_| |_|\__, |
#                              __/ |         __/ |
#                             |___/         |___/ 
if(WIN32)
  set(CPACK_GENERATOR "ZIP")
else()
  set(CPACK_GENERATOR "TGZ")
endif()
set(CPACK_PACKAGE_VERSION ${GIT_COMMIT_HASH})
set(CPACK_PACKAGE_NAME mightex1304-${CMAKE_BUILD_TYPE})
set(CPACK_PACKAGE_DIRECTORY ${CMAKE_SOURCE_DIR}/packages)
set(CPACK_STRIP_FILES TRUE)
include(CPack)

#   _____             _              __ _ _      
#  |  __ \           | |            / _(_) |     
#  | |  | | ___   ___| | _____ _ __| |_ _| | ___ 
#  | |  | |/ _ \ / __| |/ / _ \ '__|  _| | |/ _ \
#  | |__| | (_) | (__|   <  __/ |  | | | | |  __/
#  |_____/ \___/ \___|_|\_\___|_|  |_| |_|_|\___|
#                                              
# if not cross-compiling, create Dockerfile for the target platform
if(NATIVE AND NOT WIN32)
  option(ENABLE_MRUBY "Enable build of mruby in container" OFF)
  if(NOT ENABLE_MRUBY)
    message(STATUS "mruby is NOT compiled in the container")
    set(COMMENT_ENABLE_MRUBY "#")
  endif()
  option(ENABLE_OPENBLAS "Enable build of OpenBLAS in container" OFF)
  if(NOT ENABLE_OPENBLAS)
    message(STATUS "OpenBLAS is NOT compiled in the container")
    set(COMMENT_ENABLE_OPENBLAS "#")
  endif()
  option(KEEP_BUILD_DIR "Do not delete build dir in Docker container" OFF)
  if(KEEP_BUILD_DIR)
    message(STATUS "Build dir is NOT deleted after compilation")
    set(COMMENT_BUILD_DIR "#")
  endif()
  set(TARGET_NAME CACHE STRING "Select TARGET platform")
  if(NOT TARGET_NAME)
    set(TARGET_NAME "armv7")
  endif()
  set_property(CACHE TARGET_NAME PROPERTY STRINGS mipsel armv6 armv7 armv7a arm64)
  configure_file(
    ${CMAKE_HOME_DIRECTORY}/Dockerfile.in
    ${CMAKE_HOME_DIRECTORY}/Dockerfile
    @ONLY
  )
  message(STATUS "Host Dockerfile generated for target ${TARGET_NAME}")
  message(STATUS "Building of mruby (libmruby.a) is: ${ENABLE_MRUBY}")
  message(STATUS "Keep the build root directory: ${KEEP_BUILD_DIR}")
  message(STATUS "Build with: docker build -t ${TARGET_NAME} .")
  message(STATUS "then:       docker run --rm ${TARGET_NAME} > ${TARGET_NAME} && chmod a+x ${TARGET_NAME}")
endif()
