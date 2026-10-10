cmake_minimum_required(VERSION 3.16)

# ESP-IDF reads version.txt and tracks version changes for reconfiguration.
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(syndetis_finder)

# Project-local ESP-IDF 6.1 EMAC RX stability fix. Replace one source, not the
# installed SDK or the complete driver. Pins/PHY/configuration remain untouched.
if(NOT IDF_VERSION_MAJOR EQUAL 6 OR NOT IDF_VERSION_MINOR EQUAL 1)
    message(FATAL_ERROR "Review the EMAC RX patch before using a different ESP-IDF version")
endif()
idf_component_get_property(eth_library esp_eth COMPONENT_LIB)
get_target_property(eth_sources ${eth_library} SOURCES)
set(eth_original_sources ${eth_sources})
list(FILTER eth_sources EXCLUDE REGEX "(^|/)esp_eth_mac_esp_dma\\.c$")
if("${eth_original_sources}" STREQUAL "${eth_sources}")
    message(FATAL_ERROR "ESP-IDF EMAC DMA source was not found; RX patch would be inactive")
endif()
set_property(TARGET ${eth_library} PROPERTY SOURCES ${eth_sources})
target_sources(${eth_library} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/patches/esp_eth_mac_esp_dma.c")
target_include_directories(${eth_library} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/patches")

# Keep configured ping counts/timeouts/intervals. Bound unrelated raw-packet
# receive loops and observe stop requests without waiting for a matching echo.
idf_component_get_property(lwip_library lwip COMPONENT_LIB)
get_target_property(lwip_sources ${lwip_library} SOURCES)
set(lwip_original_sources ${lwip_sources})
list(FILTER lwip_sources EXCLUDE REGEX "(^|/)ping_sock\\.c$")
if("${lwip_original_sources}" STREQUAL "${lwip_sources}")
    message(FATAL_ERROR "ESP-IDF ping source was not found; cancellation patch would be inactive")
endif()
set_property(TARGET ${lwip_library} PROPERTY SOURCES ${lwip_sources})
target_sources(${lwip_library} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/patches/ping_sock.c")
target_include_directories(${lwip_library} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/patches")
