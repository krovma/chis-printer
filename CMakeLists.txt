# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)

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
set(sdkVersion 2.3.1)
set(toolchainVersion 15_2_Rel1)
set(picotoolVersion 2.3.1)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if(EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD pico2_w CACHE STRING "Board type")

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(kuper-printer C CXX ASM)

option(KP_UNIT_TEST_LCD_0 "Build the LCD color and backlight unit test" OFF)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

add_subdirectory(third_party/pico_fatfs)
add_library(tomlc17 STATIC third_party/tomlc17/tomlc17.c)
target_include_directories(tomlc17 PUBLIC ${CMAKE_CURRENT_LIST_DIR}/third_party/tomlc17)

find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(ICON_GENERATOR       ${CMAKE_CURRENT_LIST_DIR}/resource/icon/generate_icon_header.py)
set(ICON_MANIFEST        ${CMAKE_CURRENT_LIST_DIR}/resource/icon/manifest.json)
set(ICON_HEADER          ${CMAKE_CURRENT_LIST_DIR}/resource/icon/generated_icons.h)
file(GLOB ICON_PNG_FILES CONFIGURE_DEPENDS ${CMAKE_CURRENT_LIST_DIR}/resource/icon/*.png)
list(SORT ICON_PNG_FILES)

add_custom_command(
    OUTPUT ${ICON_HEADER}
    COMMAND ${Python3_EXECUTABLE} ${ICON_GENERATOR}
            --manifest ${ICON_MANIFEST}
            --output ${ICON_HEADER}
    DEPENDS ${ICON_GENERATOR} ${ICON_MANIFEST} ${ICON_PNG_FILES}
    COMMENT "Generating icon firmware header"
    VERBATIM
)
add_custom_target(generate_icon_header DEPENDS ${ICON_HEADER})

set(FONT_GENERATOR       ${CMAKE_CURRENT_LIST_DIR}/resource/font/generate_font_header.py)
set(FONT_SOURCE          ${CMAKE_CURRENT_LIST_DIR}/resource/ZhengGeDianHei-16.ttf)
set(FONT_TEXT_HEADER     ${CMAKE_CURRENT_LIST_DIR}/resource/text/display_texts.h)
set(FONT_HEADER          ${CMAKE_CURRENT_LIST_DIR}/resource/font/generated_font.h)

add_custom_command(
    OUTPUT ${FONT_HEADER}
    COMMAND ${Python3_EXECUTABLE} ${FONT_GENERATOR}
            --font ${FONT_SOURCE}
            --text-header ${FONT_TEXT_HEADER}
            --output ${FONT_HEADER}
    DEPENDS ${FONT_GENERATOR} ${FONT_SOURCE} ${FONT_TEXT_HEADER}
    COMMENT "Generating font firmware header"
    VERBATIM
)
add_custom_target(generate_font_header DEPENDS ${FONT_HEADER})

# Add executable. Default name is the project name, version 0.1

add_executable(kuper-printer
    config/display_config.cpp
    config/display_config.h
    display/palette.h
    display/ui/screen.cpp
    display/ui/screen.h
    display/ui/text.cpp
    display/ui/text.h
    display/zjy154s0800.cpp
    display/zjy154s0800.h
    main.cpp
    pin_config.h
    ${FONT_HEADER}
    ${ICON_HEADER}
    resource/text/display_texts.h
    storage/sd_card.cpp
    storage/sd_card.h
    third_party/i18n_keyval/include/i18n_keyval/i18n.hpp
    third_party/i18n_keyval/include/i18n_keyval/core/api.hpp
    third_party/i18n_keyval/include/i18n_keyval/core/common.hpp
    third_party/i18n_keyval/include/i18n_keyval/core/registry.hpp
    third_party/i18n_keyval/include/i18n_keyval/core/translator.hpp
    third_party/i18n_keyval/include/i18n_keyval/translators/basic.hpp
    third_party/i18n_keyval/src/i18n_keyval/core/api.cpp
    third_party/i18n_keyval/src/i18n_keyval/core/common.cpp
    third_party/i18n_keyval/src/i18n_keyval/core/registry.cpp
)

add_dependencies(kuper-printer generate_icon_header generate_font_header)

if(KP_UNIT_TEST_LCD_0)
    target_sources(kuper-printer PRIVATE
        unit_test/lcd_0.cpp
        unit_test/lcd_0.h
    )
    target_compile_definitions(kuper-printer PRIVATE KP_UNIT_TEST_LCD_0=1)
else()
    target_compile_definitions(kuper-printer PRIVATE KP_UNIT_TEST_LCD_0=0)
endif()

pico_set_program_name(kuper-printer "kuper-printer")
pico_set_program_version(kuper-printer "0.1")

# Modify the below lines to enable/disable output over UART/USB
pico_enable_stdio_uart(kuper-printer 0)
pico_enable_stdio_usb(kuper-printer 0)
pico_enable_stdio_rtt(kuper-printer 1)

# Add the standard library to the build
target_link_libraries(kuper-printer
    pico_stdlib
    pico_fatfs
    tomlc17
)

# Add the standard include files to the build
target_include_directories(kuper-printer PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/third_party/i18n_keyval/include
)

# Hardware used by the board-level initialization.
target_link_libraries(kuper-printer
    hardware_dma
    hardware_spi
    hardware_i2c
    hardware_pwm
    pico_rand
)

pico_add_extra_outputs(kuper-printer)

