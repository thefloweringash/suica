cmake_minimum_required (VERSION 2.8.11)
project (suica)

find_path(LIBNFC_INCLUDE_DIR nfc/nfc.h)

find_library(LIBNFC_LIBRARY nfc)

add_executable (suica suica.cc hexdump.cc)

set_property(TARGET suica PROPERTY CXX_STANDARD 11)

target_link_libraries(suica ${LIBNFC_LIBRARY})
include_directories(${LIBNFC_INCLUDE_DIR})
