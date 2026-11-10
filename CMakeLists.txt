cmake_minimum_required(VERSION 3.13)

# Game selection: doom, heretic, hexen, strife
set(GAME "heretic" CACHE STRING "Game to build: doom, heretic, hexen, strife")

# Validate game selection
if(NOT GAME MATCHES "^(doom|heretic|hexen|strife)$")
    message(FATAL_ERROR "Invalid GAME: ${GAME}. Must be doom, heretic, hexen, or strife")
endif()

string(TOUPPER ${GAME} GAME_UPPER)
message(STATUS "Game: ${GAME}")

# Board configuration variant (M1, M2, Z0, TW)
set(BOARD_VARIANT "M1" CACHE STRING "Board variant: M1, M2, Z0 or TW")
if(NOT BOARD_VARIANT MATCHES "^(M1|M2|Z0|TW)$")
    message(FATAL_ERROR "Invalid BOARD_VARIANT: ${BOARD_VARIANT}. Must be M1, M2, Z0 or TW")
endif()

if(BOARD_VARIANT STREQUAL "Z0")
    # Waveshare RP2350-PiZero: an RP2350B. The stock pico2 definition caps
    # the GPIOs at 0..29; HDMI (32..39), SD (30/31/40/43) and PSRAM CS (47)
    # need the B package and PIO's movable GPIO window.
    set(PICO_BOARD_HEADER_DIRS ${CMAKE_CURRENT_LIST_DIR}/boards CACHE PATH "" FORCE)
    set(PICO_BOARD "frank_z0_rp2350b" CACHE STRING "Pico board type" FORCE)
elseif(BOARD_VARIANT STREQUAL "TW")
    # UKTailwind Pico Computer 3: an RP2350B, SD card on 28/30/31/33 and
    # PSRAM CS on GPIO47, beyond the pico2 definition's GPIO 0..29.
    set(PICO_BOARD_HEADER_DIRS ${CMAKE_CURRENT_LIST_DIR}/boards CACHE PATH "" FORCE)
    set(PICO_BOARD "frank_tw_rp2350b" CACHE STRING "Pico board type" FORCE)
else()
    # Set board to Pico 2 (RP2350)
    set(PICO_BOARD pico2 CACHE STRING "Pico board type")
endif()

# CPU clock (252 or 504 MHz). The HDMI PIO runs at clk_sys / 252 MHz: 378
# would make that 1.5, a fractional divider the TMDS stream does not survive.
set(CPU_SPEED "504" CACHE STRING "CPU clock in MHz: 252 or 504")
if(NOT CPU_SPEED STREQUAL "252" AND NOT CPU_SPEED STREQUAL "504")
    message(FATAL_ERROR "CPU_SPEED must be 252 or 504 (the HDMI PIO divider is clk_sys / 252 MHz)")
endif()

# PSRAM speed configuration (84, 100, 133, 166 MHz target)
set(PSRAM_SPEED "133" CACHE STRING "PSRAM max frequency in MHz: 84, 100, 133, 166")

# Flash speed configuration (max safe flash frequency in MHz)
set(FLASH_SPEED "66" CACHE STRING "Flash max frequency in MHz")

# CPU voltage selection based on speed
if(CPU_SPEED GREATER_EQUAL 504)
    set(CPU_VOLTAGE "VREG_VOLTAGE_1_65")
elseif(CPU_SPEED GREATER_EQUAL 378)
    set(CPU_VOLTAGE "VREG_VOLTAGE_1_60")
else()
    set(CPU_VOLTAGE "VREG_VOLTAGE_1_50")
endif()

message(STATUS "Board: ${BOARD_VARIANT}, CPU: ${CPU_SPEED} MHz, PSRAM: ${PSRAM_SPEED} MHz, Flash: ${FLASH_SPEED} MHz, Voltage: ${CPU_VOLTAGE}")

# Inject version.txt as a compile-time string.
file(READ "${CMAKE_CURRENT_LIST_DIR}/version.txt" FRANK_VERSION_RAW)
string(STRIP "${FRANK_VERSION_RAW}" FRANK_VERSION_RAW)
# "1 1" is 1.01
string(REGEX REPLACE "[ \t]+" ";" FRANK_VERSION_LIST "${FRANK_VERSION_RAW}")
list(GET FRANK_VERSION_LIST 0 FRANK_VERSION_MAJOR)
list(GET FRANK_VERSION_LIST 1 FRANK_VERSION_MINOR)
math(EXPR FRANK_VERSION_MINOR "${FRANK_VERSION_MINOR}")
if(FRANK_VERSION_MINOR LESS 10)
    set(FRANK_VERSION_MINOR "0${FRANK_VERSION_MINOR}")
endif()
set(FRANK_VERSION "${FRANK_VERSION_MAJOR}.${FRANK_VERSION_MINOR}")

# Bench: the native USB as a serial console instead of the HID host (below)
option(FRANK_USB_CONSOLE "Bench: native USB is a serial console, not the HID host" OFF)

# Z0 (RP2350-PiZero): the USB host on the "PIO-USB" USB-C socket (GPIO28/29,
# Pico-PIO-USB) instead of the native one, as murm386's Z2.
option(FRANK_Z0_PIO_USB "Z0: USB host on the PIO-USB socket (OFF: the native USB socket)" ON)
if(BOARD_VARIANT STREQUAL "Z0" AND FRANK_Z0_PIO_USB AND NOT FRANK_USB_CONSOLE)
    set(PIO_USB_ENABLED ON)
    # Vendored Pico-PIO-USB 0.7.2 (sekigon-gonnoc, MIT) with murm386's
    # hot-replug fixes. Must be set before pico_sdk_init(): TinyUSB's rp2040
    # family script picks it up and creates the tinyusb_pico_pio_usb target.
    set(PICO_PIO_USB_PATH ${CMAKE_CURRENT_LIST_DIR}/drivers/pio_usb)
else()
    set(PIO_USB_ENABLED OFF)
endif()

include(pico_sdk_import.cmake)

project(frank-idtech1 C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

# Add fatfs
add_subdirectory(src/fatfs)
# Add drivers
add_subdirectory(drivers/sdcard)
add_subdirectory(drivers/ps2kbd)
add_subdirectory(drivers/ps2mouse)
add_subdirectory(drivers/nespad)
# Native USB is the keyboard/mouse/gamepad host (as in frank-quake2), and
# stdio goes nowhere: UART0 (GPIO0/1) is a PS/2 port on every board.
#
# A bench build (as frank-ecat's DEBUG_CONSOLE=usb) makes the native USB a
# serial console instead: logs out, keys in (the PS/2 keyboard still works,
# USB HID is off), "~BOOT~" reboots into BOOTSEL and "~RST~" restarts, so
# the board is reflashed with picotool without touching it.
if(FRANK_USB_CONSOLE)
    set(USB_HID_ENABLED OFF)
    message(STATUS "USB serial console on the native USB port (USB HID off)")
else()
    set(USB_HID_ENABLED ON)
endif()
add_subdirectory(drivers/usbhid)


# Create a library for other drivers
add_library(drivers
    drivers/video_hdmi.c
    drivers/hdmi_pio/hdmi_pio.c
    drivers/hdmi/hstx_packet.c
    drivers/psram_init.c
    drivers/psram_allocator.c
)
target_include_directories(drivers PUBLIC drivers drivers/hdmi_pio drivers/hdmi)
target_compile_definitions(drivers PRIVATE
    BOARD_${BOARD_VARIANT}
    PSRAM_MAX_FREQ_MHZ=${PSRAM_SPEED}
)
# HDMI: frank-quake2's PIO driver (drivers/hdmi_pio). Z0 drives GPIO32..39
# from pio0 (its GPIO window at 16); M1/M2 use pio1, their pio0 holds the
# PS/2 keyboard and I2S. TW's HDMI is wired as the M2's (GPIO12..19).
if(BOARD_VARIANT STREQUAL "Z0")
    target_compile_definitions(drivers PRIVATE HDMI_PIO_BASE_PIN=32 HDMI_PIO_Z0_LAYOUT=1 PIO_VIDEO=pio0)
elseif(BOARD_VARIANT STREQUAL "M2" OR BOARD_VARIANT STREQUAL "TW")
    target_compile_definitions(drivers PRIVATE HDMI_PIO_BASE_PIN=12 HDMI_PIO_Z0_LAYOUT=0 PIO_VIDEO=pio1)
else()
    target_compile_definitions(drivers PRIVATE HDMI_PIO_BASE_PIN=6 HDMI_PIO_Z0_LAYOUT=0 PIO_VIDEO=pio1)
endif()
target_compile_options(drivers PRIVATE -Ofast)
# the IRQ's loops must not become calls to memset/memcpy in flash
set_source_files_properties(drivers/hdmi_pio/hdmi_pio.c drivers/video_hdmi.c PROPERTIES COMPILE_OPTIONS "-fno-tree-loop-distribute-patterns")
target_link_libraries(drivers pico_stdlib pico_multicore hardware_dma hardware_pio hardware_spi hardware_irq hardware_timer)

# =============================================================================
# Multi-game build: all engines in one binary with symbol namespacing
# =============================================================================

# Common shared sources
file(GLOB COMMON_SOURCES src/common/*.c)

# Exclude files not needed on this platform
list(FILTER COMMON_SOURCES EXCLUDE REGEX ".*doomgeneric_.*\\.c$")
list(FILTER COMMON_SOURCES EXCLUDE REGEX ".*i_main\\.c$")
list(FILTER COMMON_SOURCES EXCLUDE REGEX ".*d_dedicated\\.c$")
list(FILTER COMMON_SOURCES EXCLUDE REGEX ".*z_native\\.c$")
list(FILTER COMMON_SOURCES EXCLUDE REGEX ".*w_file_stdc\\.c$")
list(FILTER COMMON_SOURCES EXCLUDE REGEX ".*w_file_win32\\.c$")
# Exclude sound/music modules replaced by pico versions
list(FILTER COMMON_SOURCES EXCLUDE REGEX ".*i_sdl.*\\.c$")
list(FILTER COMMON_SOURCES EXCLUDE REGEX ".*i_allegro.*\\.c$")
list(FILTER COMMON_SOURCES EXCLUDE REGEX ".*i_pcsound\\.c$")
list(FILTER COMMON_SOURCES EXCLUDE REGEX ".*i_musicpack\\.c$")
list(FILTER COMMON_SOURCES EXCLUDE REGEX ".*i_oplmusic\\.c$")
list(FILTER COMMON_SOURCES EXCLUDE REGEX ".*i_winmusic\\.c$")
list(FILTER COMMON_SOURCES EXCLUDE REGEX ".*i_flmusic\\.c$")
# i_cdmus.c is needed by Hexen (CD music stubs)
# list(FILTER COMMON_SOURCES EXCLUDE REGEX ".*i_cdmus\\.c$")
list(FILTER COMMON_SOURCES EXCLUDE REGEX ".*i_scale\\.c$")
list(FILTER COMMON_SOURCES EXCLUDE REGEX ".*i_videohr\\.c$")
list(FILTER COMMON_SOURCES EXCLUDE REGEX ".*i_joystick\\.c$")
list(FILTER COMMON_SOURCES EXCLUDE REGEX ".*i_input_stubs\\.c$")
list(FILTER COMMON_SOURCES EXCLUDE REGEX ".*i_system\\.c$")
list(FILTER COMMON_SOURCES EXCLUDE REGEX ".*midifile\\.c$")
list(FILTER COMMON_SOURCES EXCLUDE REGEX ".*m_misc\\.c$")
# Exclude network files not used on embedded (saves ~30K SRAM)
list(FILTER COMMON_SOURCES EXCLUDE REGEX ".*net_dedicated\\.c$")
list(FILTER COMMON_SOURCES EXCLUDE REGEX ".*net_server\\.c$")
list(FILTER COMMON_SOURCES EXCLUDE REGEX ".*net_client\\.c$")

# Shared compile definitions for all game targets
set(SHARED_GAME_DEFS
    BOARD_${BOARD_VARIANT}
    CPU_CLOCK_MHZ=${CPU_SPEED}
    CPU_VOLTAGE=${CPU_VOLTAGE}
    PSRAM_MAX_FREQ_MHZ=${PSRAM_SPEED}
    FLASH_MAX_FREQ_MHZ=${FLASH_SPEED}
    CMAP256
    DOOMGENERIC_RESX=320
    DOOMGENERIC_RESY=240
    FEATURE_SOUND
    USE_PICO_SOUND
    USE_OPL_MUSIC=1
    USE_EMU8950_OPL=1
    NUM_SOUND_CHANNELS=16
    NO_USE_LIBSAMPLERATE
    NO_USE_TIMIDITY
    NO_USE_GUS
    NO_USE_MUSIC_PACKS
    USE_DIRECT_MIDI_LUMP=0
    EMU8950_NO_WAVE_TABLE_MAP=1
    EMU8950_NO_TLL=1
    EMU8950_NO_FLOAT=1
    EMU8950_NO_TIMER=1
    EMU8950_NO_TEST_FLAG=1
    EMU8950_SIMPLER_NOISE=1
    EMU8950_SHORT_NOISE_UPDATE_CHECK=1
    EMU8950_LINEAR_SKIP=1
    EMU8950_LINEAR_END_OF_NOTE_OPTIMIZATION=1
    EMU8950_NO_PERCUSSION_MODE=1
    EMU8950_LINEAR=1
    EMU8950_ASM=0
    EMU8950_SLOT_RENDER=1
    EMU8950_NO_RATECONV=1
    PICO_ON_DEVICE=1
    PICO_MALLOC_PANIC=0
    PICO_CORE1_STACK_SIZE=0
    FRANK_VERSION="${FRANK_VERSION}"
)

# Game-specific sources (each game compiled with symbol prefix)
file(GLOB DOOM_GAME_SOURCES src/doom/*.c)
file(GLOB HERETIC_GAME_SOURCES src/heretic/*.c)
file(GLOB HEXEN_GAME_SOURCES src/hexen/*.c)
file(GLOB STRIFE_GAME_SOURCES src/strife/*.c)

# Shared game include/compile setup (avoids linking pico_stdlib into OBJECT libs)
set(GAME_INCLUDE_DIRS
    src src/common src/pico src/fatfs src/opl
    drivers drivers/sdcard drivers/ps2mouse
    $<TARGET_PROPERTY:pico_stdlib,INTERFACE_INCLUDE_DIRECTORIES>
)

# Doom object library (namespaced with DOOM_ prefix)
add_library(doom_game OBJECT ${DOOM_GAME_SOURCES})
target_include_directories(doom_game PRIVATE src/doom ${GAME_INCLUDE_DIRS})
target_compile_options(doom_game PRIVATE -Ofast -fdata-sections -ffunction-sections -include ${CMAKE_SOURCE_DIR}/src/doom/doom_prefix.h)
target_compile_definitions(doom_game PRIVATE GAME_DOOM=1 DOOM=1 ${SHARED_GAME_DEFS}
    $<TARGET_PROPERTY:pico_stdlib,INTERFACE_COMPILE_DEFINITIONS>)

# Heretic object library (namespaced with HERETIC_ prefix)
add_library(heretic_game OBJECT ${HERETIC_GAME_SOURCES})
target_include_directories(heretic_game PRIVATE src/heretic ${GAME_INCLUDE_DIRS})
target_compile_options(heretic_game PRIVATE -Ofast -fdata-sections -ffunction-sections -include ${CMAKE_SOURCE_DIR}/src/heretic/heretic_prefix.h)
target_compile_definitions(heretic_game PRIVATE GAME_HERETIC=1 HERETIC=1 ${SHARED_GAME_DEFS}
    $<TARGET_PROPERTY:pico_stdlib,INTERFACE_COMPILE_DEFINITIONS>)

# Hexen object library (namespaced with HEXEN_ prefix)
add_library(hexen_game OBJECT ${HEXEN_GAME_SOURCES})
target_include_directories(hexen_game PRIVATE src/hexen ${GAME_INCLUDE_DIRS})
target_compile_options(hexen_game PRIVATE -Ofast -fdata-sections -ffunction-sections -include ${CMAKE_SOURCE_DIR}/src/hexen/hexen_prefix.h)
target_compile_definitions(hexen_game PRIVATE GAME_HEXEN=1 HEXEN=1 ${SHARED_GAME_DEFS}
    $<TARGET_PROPERTY:pico_stdlib,INTERFACE_COMPILE_DEFINITIONS>)

# Strife object library (namespaced with STRIFE_ prefix)
add_library(strife_game OBJECT ${STRIFE_GAME_SOURCES})
target_include_directories(strife_game PRIVATE src/strife ${GAME_INCLUDE_DIRS})
target_compile_options(strife_game PRIVATE -Ofast -fdata-sections -ffunction-sections -include ${CMAKE_SOURCE_DIR}/src/strife/strife_prefix.h)
target_compile_definitions(strife_game PRIVATE GAME_STRIFE=1 STRIFE=1 ${SHARED_GAME_DEFS}
    $<TARGET_PROPERTY:pico_stdlib,INTERFACE_COMPILE_DEFINITIONS>)

# Pico platform + OPL sources
set(PICO_SOURCES
    src/pico/i_picosound.c
    src/pico/i_oplmusic.c
    src/midifile.c
    src/opl/emu8950.c
    src/opl/emuadpcm.c
    src/opl/opl_api.c
    src/opl/opl_pico.c
    src/opl/slot_render.cpp
)

add_executable(frank-idtech1
    src/main.c
    src/doomgeneric_rp2350.c
    src/fui_video.c
    src/fui_gfx.c
    src/fui_input.c
    src/fui_shell.c
    src/fui_menu.c
    src/fui_cfg.c
    src/font6x8.c
    drivers/hid_doomkeys.c
    src/doomgeneric_fatfs/w_file_fatfs.c
    src/doomgeneric_fatfs/m_misc_fatfs.c
    src/doomgeneric_fatfs/stdio_fatfs.c
    src/pico/audio.c
    ${COMMON_SOURCES}
    ${PICO_SOURCES}
    $<TARGET_OBJECTS:doom_game>
    $<TARGET_OBJECTS:heretic_game>
    $<TARGET_OBJECTS:hexen_game>
    $<TARGET_OBJECTS:strife_game>
)

# Custom linker script with BSS OVERLAY (both games share same RAM region)
pico_set_linker_script(frank-idtech1 ${CMAKE_SOURCE_DIR}/memmap_multigame.ld)

# Generate PIO header for I2S audio (must be after add_executable)
add_subdirectory(drivers/audio)

target_include_directories(frank-idtech1 PRIVATE
    src
    src/common
    src/pico
    src/fatfs
    src/opl
    drivers
    drivers/sdcard
    drivers/ps2mouse
)

target_compile_options(frank-idtech1 PRIVATE -Ofast)

target_compile_definitions(frank-idtech1 PRIVATE
    ${SHARED_GAME_DEFS}
    PICO_MALLOC_PANIC=0
)

target_link_options(frank-idtech1 PRIVATE -Wl,-Map=frank-idtech1.map -Wl,--gc-sections)

# Set peripheral pins based on board variant
if(BOARD_VARIANT STREQUAL "M1")
    target_compile_definitions(frank-idtech1 PRIVATE
        # I2S Audio
        PICO_AUDIO_I2S_DATA_PIN=26
        PICO_AUDIO_I2S_CLOCK_PIN_BASE=27
        # SD Card
        SDCARD_PIN_SPI0_CS=5
        SDCARD_PIN_SPI0_SCK=2
        SDCARD_PIN_SPI0_MOSI=3
        SDCARD_PIN_SPI0_MISO=4
    )
elseif(BOARD_VARIANT STREQUAL "Z0")
    # Waveshare RP2350-PiZero (murm386 "Z2", frank-quake2 "Z0")
    target_compile_definitions(frank-idtech1 PRIVATE
        # I2S Audio (a PCM5102A module; sound is not brought up yet). pio0 is
        # the video's (its GPIO window starts at 16), pio1 the PS/2 keyboard's.
        PICO_AUDIO_I2S_DATA_PIN=10
        PICO_AUDIO_I2S_CLOCK_PIN_BASE=11
        AUDIO_PIO=pio2
        # SD Card on SPI1
        SDCARD_SPI_BUS=spi1
        SDCARD_PIN_SPI0_CS=43
        SDCARD_PIN_SPI0_SCK=30
        SDCARD_PIN_SPI0_MOSI=31
        SDCARD_PIN_SPI0_MISO=40
        # NES/SNES pad (murm386)
        NESPAD_GPIO_CLK=4
        NESPAD_GPIO_LATCH=7
        NESPAD_GPIO_DATA=6
    )
    if(PIO_USB_ENABLED)
        # the "PIO-USB" socket: D+ GPIO28, D- GPIO29, a whole PIO (pio1)
        target_compile_definitions(frank-idtech1 PRIVATE PIO_USB_DP_PIN=28 PIO_USB_PIO=1)
    endif()
elseif(BOARD_VARIANT STREQUAL "TW")
    # UKTailwind Pico Computer 3 (https://github.com/UKTailwind/Pico-Computer-3)
    target_compile_definitions(frank-idtech1 PRIVATE
        # I2S Audio: the on-board PCM5102A (BCK 10, LRCK 11, DIN 22)
        PICO_AUDIO_I2S_DATA_PIN=22
        PICO_AUDIO_I2S_CLOCK_PIN_BASE=10
        # SD Card on SPI1
        SDCARD_SPI_BUS=spi1
        SDCARD_PIN_SPI0_CS=33
        SDCARD_PIN_SPI0_SCK=30
        SDCARD_PIN_SPI0_MOSI=31
        SDCARD_PIN_SPI0_MISO=28
        # No PS/2 or pad port on the board: PS/2 (as M2, GPIO0..3) and the
        # NES/SNES pad (as Z0, GPIO4/6/7) on the "EXTERNAL I/O" header
        NESPAD_GPIO_CLK=4
        NESPAD_GPIO_LATCH=7
        NESPAD_GPIO_DATA=6
    )
elseif(BOARD_VARIANT STREQUAL "M2")
    target_compile_definitions(frank-idtech1 PRIVATE
        # I2S Audio
        PICO_AUDIO_I2S_DATA_PIN=9
        PICO_AUDIO_I2S_CLOCK_PIN_BASE=10
        # SD Card
        SDCARD_PIN_SPI0_CS=5
        SDCARD_PIN_SPI0_SCK=6
        SDCARD_PIN_SPI0_MOSI=7
        SDCARD_PIN_SPI0_MISO=4
        # NES/SNES pad port (murmgenesis). M1 has none here: its pins
        # GPIO14/15 are this firmware's PS/2 mouse.
        NESPAD_GPIO_CLK=20
        NESPAD_GPIO_LATCH=21
        NESPAD_GPIO_DATA=26
    )
endif()

target_link_libraries(frank-idtech1
    pico_stdlib
    pico_multicore
    hardware_clocks
    hardware_spi
    hardware_dma
    hardware_pio
    hardware_pwm
    hardware_irq
    hardware_interp
    hardware_exception
    hardware_flash
    sdcard
    ps2kbd
    ps2mouse
    usbhid
    nespad
    fatfs
    drivers
)

# Wrap stdio and memory allocation (redirect to PSRAM/FatFS)
target_link_options(frank-idtech1 PRIVATE
    -Wl,--wrap=fopen
    -Wl,--wrap=fclose
    -Wl,--wrap=fread
    -Wl,--wrap=fgetc
    -Wl,--wrap=fwrite
    -Wl,--wrap=fprintf
    -Wl,--wrap=fscanf
    -Wl,--wrap=fseek
    -Wl,--wrap=ftell
    -Wl,--wrap=remove
    -Wl,--wrap=rename
)

# No stdio: USB is the HID host, UART0's pins are PS/2. A bench build can
# take UART0 (GPIO0/1, 115200) for a console instead: logs, keys typed there,
# ~BOOT~ to reboot into BOOTSEL, ~RST~ to restart. The PS/2 port on those
# pins (M2: mouse, M1: keyboard) is then off.
# TW: the console is UART1 on GPIO8/9 (its CH340C "USB CONSOLE" socket),
# both PS/2 ports stay.
option(FRANK_UART_CONSOLE "UART0 console on GPIO0/1 instead of that PS/2 port" OFF)
# A bench with a debug probe's UART on GPIO0/1: the M2's PS/2 mouse driver
# holds GPIO0 low, which the probe takes for an endless break and stops
# answering SWD. This leaves GPIO0/1 alone (M2; USB mice still work).
option(FRANK_NO_PS2_MOUSE "Leave the M2's PS/2 mouse pins (GPIO0/1) undriven" OFF)
if(FRANK_NO_PS2_MOUSE)
    target_compile_definitions(frank-idtech1 PRIVATE FRANK_NO_PS2_MOUSE=1)
endif()
if(FRANK_USB_CONSOLE)
    pico_enable_stdio_usb(frank-idtech1 1)
    target_compile_definitions(frank-idtech1 PRIVATE FRANK_USB_CONSOLE=1)
else()
    pico_enable_stdio_usb(frank-idtech1 0)
endif()
if(FRANK_UART_CONSOLE)
    pico_enable_stdio_uart(frank-idtech1 1)
    target_compile_definitions(frank-idtech1 PRIVATE FRANK_UART_CONSOLE=1)
    if(BOARD_VARIANT STREQUAL "TW")
        message(STATUS "UART1 console on GPIO8/9 (the on-board CH340C)")
    else()
        message(STATUS "UART0 console on GPIO0/1 (PS/2 on those pins off)")
    endif()
else()
    pico_enable_stdio_uart(frank-idtech1 0)
endif()

pico_add_extra_outputs(frank-idtech1)
