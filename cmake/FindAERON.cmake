# - Try to find libaeron include dirs and libraries
#
# Usage of this module as follows:
#
#     find_package(Aeron)
#
# Variables used by this module, they can change the default behaviour and need
# to be set before calling find_package:
#
#  AERON_ROOT_DIR             Set this variable to the root installation of
#                            libAERON if the module has problems finding the
#                            proper installation path.
#
# Variables defined by this module:
#
#  AERON_FOUND                System has libaeron, include and library dirs found
#  AERON_INCLUDE_DIR          The libaeron include directories.
#  AERON_LIBRARY              The libaeron library

find_path(AERON_ROOT_DIR
    NAMES include/aeron/aeronc.h
)

find_path(AERON_INCLUDE_DIR
    NAMES aeron/aeronc.h
    HINTS ${AERON_ROOT_DIR}/include
)

set (HINT_DIR ${AERON_ROOT_DIR}/lib)

# On x64 windows, we should look also for the .lib at /lib/x64/
# as this is the default path for the WinAERON developer's pack
if (${CMAKE_SIZEOF_VOID_P} EQUAL 8 AND WIN32)
    set (HINT_DIR ${AERON_ROOT_DIR}/lib/x64/ ${HINT_DIR})
endif ()

find_library(AERON_LIBRARY
    NAMES aeron
    HINTS ${HINT_DIR}
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(AERON DEFAULT_MSG
    AERON_LIBRARY
    AERON_INCLUDE_DIR
)

mark_as_advanced(
    AERON_ROOT_DIR
    AERON_INCLUDE_DIR
    AERON_LIBRARY
)