# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)

set(CMAKE_BUILD_TYPE RELEASE)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Initialise pico_sdk from installed location
# (note this can come from environment, CMake cache etc)

# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 1.5.1)
set(toolchainVersion 13_2_Rel1)
set(picotoolVersion 2.0.0)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD pico CACHE STRING "Board type")

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(rp2040SFU C CXX ASM)

set(PICO_CMAKE_CMSIS_ENABLED 1)
set(PICO_FLASH_SIZE_BYTES 4194304)         # 4Mb for W25Q32
set(PICO_USE_NEWLIB_NANO 1)
set(PICO_BOOT_STAGE2_FILE ${PICO_SDK_PATH}/src/rp2_common/boot_stage2/boot2_w25q080.S)
add_compile_definitions(PICO_FLASH_SIZE_BYTES=0x400000)
# set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE) // TODO: fix linker error: undefined reference to `__wrap_putchar'

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

add_compile_options(
    -O2
    -ffunction-sections
    -fdata-sections
    -fno-asynchronous-unwind-tables
    -fno-unwind-tables
    # for C++:
    # -fno-exceptions 
    # -fno-rtti
    )


# Add executable. Default name is the project name, version 0.1

add_executable(rp2040SFU 
    rp2040SFU.c 
    usart_mini.c
    crc32.c
    packet_receiver.c
    sfu_commands.c
    )    

pico_set_program_name(rp2040SFU "rp2040SFU")
pico_set_program_version(rp2040SFU "0.1")

# add_compile_definitions(PICO_NO_BINARY_INFO=1)
    
target_link_options(rp2040SFU PRIVATE 
    -Wl,--print-memory-usage
    -Wl,--gc-sections
    -Wl,--build-id=none
    )

# Modify the below lines to enable/disable output over UART/USB
# pico_enable_stdio_uart(rp2040SFU 0)
# pico_enable_stdio_usb(rp2040SFU 1)

pico_set_linker_script(rp2040SFU ${CMAKE_CURRENT_LIST_DIR}/memmap_size_only_0x10000.ld)

# Add the standard library to the build
target_link_libraries(rp2040SFU
        pico_stdlib   
        cmsis_core
        pico_multicore
        hardware_dma
        hardware_flash
        ) 

# Add the standard include files to the build
target_include_directories(rp2040SFU PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
        ${PICO_SDK_PATH}/src/rp2_common/cmsis/stub/CMSIS/Core/Include
        ${PICO_SDK_PATH}/src/rp2_common/cmsis/stub/CMSIS/Device/RaspberryPi/RP2040/Include
)

pico_add_extra_outputs(rp2040SFU)

set(RP2040_FLASH_BASE 0x10000000)
set(RP2040_BOOT_END  0x10010000)  # 0x10000000 + 64KiB

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
  COMMAND ${CMAKE_OBJCOPY}
          -O binary
          --pad-to ${RP2040_BOOT_END}
          --gap-fill 0xFF
          $<TARGET_FILE:${PROJECT_NAME}>
          ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}_padded_64k.bin
  COMMENT "Make padded 64KiB (0xFF) .bin for boot region"
)