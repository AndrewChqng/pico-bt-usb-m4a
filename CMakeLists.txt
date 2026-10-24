cmake_minimum_required(VERSION 3.13)

option(BUILD_HOST_TESTS "Build host-native verification tests instead of RP2040 UF2 firmware" OFF)

if(BUILD_HOST_TESTS)
    project(pico_bt_usb_m4a_tests C)
    set(CMAKE_C_STANDARD 11)
    enable_testing()

    add_executable(test_suite
        tests/test_suite.c
        src/aac_latm.c
        src/m4a_container.c
        src/fat32_virtual.c
        src/stream_ringbuf.c
    )
    target_include_directories(test_suite PRIVATE include)
    target_compile_options(test_suite PRIVATE -Wall -Wextra -Werror -O2)

    add_test(NAME verify_latm_m4a_fat32 COMMAND test_suite)
else()
    set(PICO_BOARD pico_w CACHE STRING "Board type")
    include(pico_sdk_import.cmake)

    project(pico_bt_usb_m4a C CXX ASM)
    set(CMAKE_C_STANDARD 11)
    set(CMAKE_CXX_STANDARD 17)

    pico_sdk_init()

    add_executable(pico_bt_usb_m4a
        src/main.c
        src/bt_a2dp_sink.c
        src/usb_descriptors.c
        src/usb_msc_driver.c
        src/aac_latm.c
        src/m4a_container.c
        src/fat32_virtual.c
        src/stream_ringbuf.c
    )

    target_include_directories(pico_bt_usb_m4a PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/include
    )

    target_link_libraries(pico_bt_usb_m4a
        pico_stdlib
        pico_multicore
        pico_flash
        pico_unique_id
        pico_btstack_classic
        pico_btstack_cyw43
        pico_cyw43_arch_none
        tinyusb_device
        tinyusb_board
    )

    # Disable stdio over USB since we present a pure Mass Storage Class device
    pico_enable_stdio_usb(pico_bt_usb_m4a 0)
    pico_enable_stdio_uart(pico_bt_usb_m4a 1)

    pico_add_extra_outputs(pico_bt_usb_m4a)
endif()
