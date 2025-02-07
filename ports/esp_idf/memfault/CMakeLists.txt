if (CONFIG_MEMFAULT_AUTOMATIC_INIT)
  message(FATAL_ERROR "CONFIG_MEMFAULT_AUTOMATIC_INIT has been deprecated.
    Please complete the following steps:
    1. Remove CONFIG_MEMFAULT_AUTOMATIC_INIT=y from sdkconfig.default if present
    2. Delete your project's generated sdkconfig (be sure to save any in-progress changes)
    3. Update your application to call memfault_boot during initialization
    For more information please see https://docs.memfault.com/docs/mcu/esp32-guide")
endif()

set(MEMFAULT_SDK_ROOT ${CMAKE_CURRENT_LIST_DIR}/../../..)

include(${MEMFAULT_SDK_ROOT}/cmake/Memfault.cmake)

# Select RISCV or XTENSA architecture, depending on target chip
if(DEFINED IDF_VERSION_MAJOR)
  # idf_build_get_property was added in 4.0. RISC-V chips weren't added until
  # v4.3, but we can rely on the build target for figuring out the architecture.
  if(IDF_VERSION_MAJOR VERSION_GREATER_EQUAL 4)
    # Get target architecture to pass to the memfault_library initialization.
    # IDF_TARGET_ARCH was added in 5.0, so use the older IDF_TARGET variable.
    idf_build_get_property(target IDF_TARGET)
    if("${target}" STREQUAL "esp32" OR "${target}" STREQUAL "esp32s2" OR "${target}" STREQUAL "esp32s3")
      set(ESP_ARCH "ARCH_XTENSA")
    else()
      set(ESP_ARCH "ARCH_RISCV")
    endif()
  else()
    # For older versions of esp-idf, we assume the architecture is XTENSA
    set(ESP_ARCH "ARCH_XTENSA")
  endif()
else()
  # If the IDF_VERSION_MAJOR variable is not defined, we assume the architecture is XTENSA- this is
  # a pretty old version of esp-idf, much earlier than when RISC-V support was added.
  set(ESP_ARCH "ARCH_XTENSA")
endif()

list(APPEND MEMFAULT_COMPONENTS core util panics demo http metrics)
memfault_library(${MEMFAULT_SDK_ROOT} MEMFAULT_COMPONENTS
  MEMFAULT_COMPONENTS_SRCS MEMFAULT_COMPONENTS_INC_FOLDERS ${ESP_ARCH})

# v4.0 and greater expose the IDF_VERSION in cmake. If we can't find
# it, we assume the end user is on v3.x

include($ENV{IDF_PATH}/tools/cmake/version.cmake OPTIONAL)

# Version 3.3.5 and later provide IDF_VERSION_MAJOR.
if(DEFINED IDF_VERSION_MAJOR)
  if(IDF_VERSION_MAJOR VERSION_EQUAL 5)
    set(MEMFAULT_ESP_IDF_PORT v5.x)
  elseif(IDF_VERSION_MAJOR VERSION_EQUAL 4)
    set(MEMFAULT_ESP_IDF_PORT v4.x)
  elseif(IDF_VERSION_MAJOR VERSION_EQUAL 3)
    set(MEMFAULT_ESP_IDF_PORT v3.x)
  else()
    message(FATAL_ERROR "IDF_VERSION_MAJOR=${IDF_VERSION_MAJOR} provided is not currenly supported")
  endif()
else()
  message(STATUS "No IDF_VERSION_MAJOR provided, using esp-idf v3.x Memfault port")
  set(MEMFAULT_ESP_IDF_PORT v3.x)
endif()

# esp-idf version specific porting files
list(APPEND MEMFAULT_COMPONENTS_SRCS
  ${MEMFAULT_ESP_IDF_PORT}/memfault_esp_spi_flash.c
)

include(${CMAKE_CURRENT_LIST_DIR}/${MEMFAULT_ESP_IDF_PORT}/Memfault-esp-idf-compat.cmake)

# This directory holds ports that are common across v3.x and v4.x esp-idf releases
set(MEMFAULT_ESP_IDF_PORT_COMMON common)
list(APPEND MEMFAULT_COMPONENTS_SRCS
    ${MEMFAULT_ESP_IDF_PORT_COMMON}/memfault_fault_handler.c
    ${MEMFAULT_ESP_IDF_PORT_COMMON}/memfault_platform_core.c
    ${MEMFAULT_ESP_IDF_PORT_COMMON}/memfault_platform_coredump.c
    ${MEMFAULT_ESP_IDF_PORT_COMMON}/memfault_platform_debug_log.c
    ${MEMFAULT_ESP_IDF_PORT_COMMON}/memfault_platform_demo_cli_cmds.c
    ${MEMFAULT_ESP_IDF_PORT_COMMON}/memfault_platform_http_client.c
    ${MEMFAULT_ESP_IDF_PORT_COMMON}/memfault_platform_http_client_buffer.c
    ${MEMFAULT_ESP_IDF_PORT_COMMON}/memfault_platform_metrics.c
)

list(APPEND MEMFAULT_COMPONENTS_INC_FOLDERS
   include
   include/${MEMFAULT_ESP_IDF_PORT}
   $ENV{MEMFAULT_PLATFORM_EXTRA_INCLUDES}
)

# For version >= 4.4.3, we can collect smaller coredumps by default
# by prioritizing active stack and FreeRTOS regions first. ESP-IDF < 4.4.3
# uses a simpler scheme collecting all of DRAM. See
# common/memfault_platform_coredump.c for more info.
# Note: CMake does not short-circuit logic statements, nested ifs required
# Note: ENV{IDF_VERSION} added in esp-idf 4.4.3
if (DEFINED ENV{IDF_VERSION})
  if ($ENV{IDF_VERSION} VERSION_GREATER_EQUAL "4.4.3")
    list(APPEND MEMFAULT_COMPONENTS_SRCS
      ${MEMFAULT_SDK_ROOT}/ports/freertos/src/memfault_freertos_ram_regions.c
    )

    list(APPEND MEMFAULT_COMPONENTS_INC_FOLDERS
      ${MEMFAULT_SDK_ROOT}/ports/include/
    )

    # Add a linker fragment to place FreeRTOS timers and task objects in the same area of dram0.bss
    set(COMPONENT_ADD_LDFRAGMENTS "${MEMFAULT_ESP_IDF_PORT_COMMON}/memfault_esp_freertos.lf")
  endif()
endif()


# Register Memfault SDK Component
set(COMPONENT_SRCS ${MEMFAULT_COMPONENTS_SRCS})
set(COMPONENT_ADD_INCLUDEDIRS ${MEMFAULT_COMPONENTS_INC_FOLDERS})
list(APPEND COMPONENT_REQUIRES
    ${MEMFAULT_ESP_IDF_VERSION_SPECIFIC_REQUIRES}
    $ENV{MEMFAULT_PLATFORM_PORT_COMPONENTS}
    freertos
    heap
    log
    soc
    spi_flash
    console
    driver
)

if($ENV{MEMFAULT_ESP_HTTP_CLIENT_ENABLE})
list(APPEND COMPONENT_REQUIRES esp_http_client esp_https_ota)
endif()
register_component()

# sdk_overrides/esp_http_client.c needs the (private) esp_http header files:
get_filename_component(this_directory_name . ABSOLUTE DIRECTORY)
get_filename_component(this_directory_name ${this_directory_name} NAME)
mflt_esp32_component_get_target(this_component ${this_directory_name})

component_compile_options(-DMEMFAULT_ESP_HTTP_CLIENT_ENABLE=$ENV{MEMFAULT_ESP_HTTP_CLIENT_ENABLE})

# We will intercept the panic handlers enabled by CONFIG_ESP32_ENABLE_COREDUMP_TO_FLASH
# and run the Memfault Fault Handler instead.
#
# Benefits here are:
#   FreeRTOS task list is walked server side instead of on device (so you can get crash data even if the lists are corrupted)
#   Much more flexibility in debug information collected (e.g. all RAM, just the current stack trace, select stacks and variables)
#   Data can be posted directly from device to Memfault cloud for deduplication and analysis
target_link_libraries(${this_component} INTERFACE "-Wl,--wrap=esp_core_dump_to_flash -Wl,--wrap=esp_core_dump_init -Wl,--wrap=esp_core_dump_image_get")

# Include a linker script fragment to support compact logs, when enabled
get_filename_component(compact_log_linker_script ${MEMFAULT_ESP_IDF_PORT_COMMON}/memfault_compact_log.ld ABSOLUTE)
target_link_libraries(
    ${this_component}
    INTERFACE
    -T ${compact_log_linker_script}
)

# Link required libraries and add compiler flags to enable FreeRTOS region collection
# in >= 4.4.3. Note: CMake does not short-circuit logic statements, nested ifs required
# Note: ENV{IDF_VERSION} added in esp-idf 4.4.0
if(DEFINED ENV{IDF_VERSION})
  if ($ENV{IDF_VERSION} VERSION_GREATER_EQUAL "4.4.3")
    # Policy change requires CMake v3.13+
    cmake_minimum_required(VERSION 3.13)

    # First set new policy for target_link_libraries, this resolves a warning when using on
    # targets not created in this directory
    cmake_policy(SET CMP0079 NEW)

    # Get the name of the ESP FreeRTOS target/library
    idf_component_get_property(freertos_lib freertos COMPONENT_LIB)

    # Link this component to FreeRTOS, use INTERFACE because we're only sharing headers
    target_link_libraries(${freertos_lib} INTERFACE ${this_component})

    # Lastly ensure that our FreeRTOS trace hooks are defined first by adding this
    # compile option to the FreeRTOS target to include with all source
    # This method is an alternative to #include within FreeRTOSConfig.h which esp-idf
    # makes very difficult to do.
    get_filename_component(freertos_trace_header ${MEMFAULT_SDK_ROOT}/ports/include/memfault/ports/freertos_trace.h ABSOLUTE)
    target_compile_options(${freertos_lib} INTERFACE -include ${freertos_trace_header})
  endif()
endif()
