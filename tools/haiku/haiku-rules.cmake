# Loaded by CMake after its compiler and platform modules
# (CMAKE_USER_MAKE_RULES_OVERRIDE, set in toolchain.cmake).
#
# A Haiku executable is a position-independent image the runtime loader
# relocates like a library, so its objects must be -fPIC (the compiler's
# default). CMake compiles the sources of an executable with -fPIE when
# POSITION_INDEPENDENT_CODE is on, which makes x86_64 PC32 relocations
# against preemptible symbols: "relocation R_X86_64_PC32 ... can not be
# used when making a shared object".
foreach(_bs_lang C CXX)
    set(CMAKE_${_bs_lang}_COMPILE_OPTIONS_PIE -fPIC)
    set(CMAKE_${_bs_lang}_LINK_OPTIONS_PIE "")
    set(CMAKE_${_bs_lang}_LINK_OPTIONS_NO_PIE "")
endforeach()
