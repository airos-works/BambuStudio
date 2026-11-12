# CMake toolchain for cross-building Bambu Studio and its dependencies for
# air/OS (Haiku) on Linux with an airos-ci SDK:
#
#   cmake -S . -B <build> -DCMAKE_TOOLCHAIN_FILE=tools/haiku/toolchain.cmake \
#         -DHAIKU_ARCH=x86_64|arm64
#
# tools/haiku/build-deps.sh and tools/haiku/build.sh do this for you. The
# variables below may also come from the environment:
#
#   HAIKU_ARCH   x86_64 (default) or arm64
#   HAIKU_SDK    the SDK: env.sh, sysroot/, shared-unwinder.specs
#                (default /data1/airos/sdk/${HAIKU_ARCH})
#   AIROS_DEPS   extra development packages laid out as a second sysroot
#                (curl, OpenSSL, the arm64 picture and GL libraries):
#                default /data1/airos/deps/${HAIKU_ARCH}
#   BS_DEPS      prefix of the static dependencies built by build-deps.sh
#                (default /data1/scratch/bambustudio/deps/${HAIKU_ARCH})
set(CMAKE_SYSTEM_NAME Haiku)

macro(_bs_setting name default)
    if(NOT ${name})
        set(${name} "$ENV{${name}}")
    endif()
    if(NOT ${name})
        set(${name} "${default}")
    endif()
endmacro()
_bs_setting(HAIKU_ARCH x86_64)
_bs_setting(HAIKU_SDK /data1/airos/sdk/${HAIKU_ARCH})
_bs_setting(AIROS_DEPS /data1/airos/deps/${HAIKU_ARCH})
_bs_setting(BS_DEPS /data1/scratch/bambustudio/deps/${HAIKU_ARCH})
# try_compile projects see the same settings.
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES HAIKU_ARCH HAIKU_SDK AIROS_DEPS BS_DEPS)

if(HAIKU_ARCH STREQUAL "arm64")
    set(CMAKE_SYSTEM_PROCESSOR aarch64)
elseif(HAIKU_ARCH STREQUAL "x86_64")
    set(CMAKE_SYSTEM_PROCESSOR x86_64)
else()
    message(FATAL_ERROR "HAIKU_ARCH must be x86_64 or arm64, not '${HAIKU_ARCH}'")
endif()

file(STRINGS "${HAIKU_SDK}/env.sh" _bs_cross REGEX "^CROSS=")
string(REGEX REPLACE "^CROSS=" "" HAIKU_CROSS "${_bs_cross}")
file(STRINGS "${HAIKU_SDK}/env.sh" _bs_triplet REGEX "^TRIPLET=")
string(REGEX REPLACE "^TRIPLET=" "" HAIKU_TRIPLET "${_bs_triplet}")
# deps/GMP and deps/MPFR name the autoconf host with this.
set(TOOLCHAIN_PREFIX ${HAIKU_TRIPLET})

set(CMAKE_SYSROOT ${HAIKU_SDK}/sysroot)
set(CMAKE_USER_MAKE_RULES_OVERRIDE ${CMAKE_CURRENT_LIST_DIR}/haiku-rules.cmake)
set(CMAKE_C_COMPILER ${HAIKU_CROSS}gcc)
set(CMAKE_CXX_COMPILER ${HAIKU_CROSS}g++)
set(CMAKE_AR ${HAIKU_CROSS}ar CACHE FILEPATH "")
set(CMAKE_RANLIB ${HAIKU_CROSS}ranlib CACHE FILEPATH "")
set(CMAKE_STRIP ${HAIKU_CROSS}strip CACHE FILEPATH "")

# The extra development packages are a second system tree: their headers are
# searched after the SDK's, their libraries are link candidates.
set(_bs_extra ${AIROS_DEPS}/boot/system)
set(_bs_flags "-isystem ${_bs_extra}/develop/headers")
set(CMAKE_C_FLAGS_INIT "${_bs_flags}")
set(CMAKE_CXX_FLAGS_INIT "${_bs_flags}")
# The cross compiler links only libgcc.a, whose private unwinder copy breaks
# C++ exceptions across libstdc++; the SDK's specs link libgcc_s first.
set(_bs_link "-L${_bs_extra}/develop/lib -L${_bs_extra}/lib -Wl,-rpath-link,${_bs_extra}/lib -Wl,-rpath-link,${CMAKE_SYSROOT}/boot/system/lib")
if(EXISTS ${HAIKU_SDK}/shared-unwinder.specs)
    set(_bs_link "-specs=${HAIKU_SDK}/shared-unwinder.specs ${_bs_link}")
endif()
set(CMAKE_EXE_LINKER_FLAGS_INIT "${_bs_link}")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "${_bs_link}")
set(CMAKE_MODULE_LINKER_FLAGS_INIT "${_bs_link}")

# find_*(): the dependency prefix first, then the extra packages and the SDK
# (CMake's Haiku platform knows the develop/headers and develop/lib layout
# below each root).
set(CMAKE_FIND_ROOT_PATH ${BS_DEPS} ${AIROS_DEPS})
list(APPEND CMAKE_PREFIX_PATH ${BS_DEPS})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
