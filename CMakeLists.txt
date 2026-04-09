cmake_minimum_required(VERSION 3.13)

# Game selection: doom, heretic, hexen, strife
set(GAME "heretic" CACHE STRING "Game to build: doom, heretic, hexen, strife")

# Validate game selection
if(NOT GAME MATCHES "^(doom|heretic|hexen|strife)$")
    message(FATAL_ERROR "Invalid GAME: ${GAME}. Must be doom, heretic, hexen, or strife")
endif()

string(TOUPPER ${GAME} GAME_UPPER)
message(STATUS "Game: ${GAME}")

# Set board to Pico 2 (RP2350)
set(PICO_BOARD pico2 CACHE STRING "Pico board type")

# Board configuration variant (M1, M2)
set(BOARD_VARIANT "M1" CACHE STRING "Board variant: M1 or M2")

# CPU overclock configuration (252, 378, 504 MHz)
# 252 = no overclock (default), 378 = medium, 504 = high
set(CPU_SPEED "378" CACHE STRING "CPU clock in MHz: 252, 378, 504")

# PSRAM speed configuration (84, 100, 133, 166 MHz target)
set(PSRAM_SPEED "133" CACHE STRING "PSRAM max frequency in MHz: 84, 100, 133, 166")

# CPU voltage selection based on speed
if(CPU_SPEED GREATER_EQUAL 504)
    set(CPU_VOLTAGE "VREG_VOLTAGE_1_65")
elseif(CPU_SPEED GREATER_EQUAL 378)
    set(CPU_VOLTAGE "VREG_VOLTAGE_1_60")
else()
    set(CPU_VOLTAGE "VREG_VOLTAGE_1_50")
endif()

message(STATUS "Board: ${BOARD_VARIANT}, CPU: ${CPU_SPEED} MHz, PSRAM: ${PSRAM_SPEED} MHz, Voltage: ${CPU_VOLTAGE}")

# Inject version.txt as a compile-time string.
file(READ "${CMAKE_CURRENT_LIST_DIR}/version.txt" FRANK_VERSION_RAW)
string(STRIP "${FRANK_VERSION_RAW}" FRANK_VERSION_RAW)
string(REGEX REPLACE "[ \t]+" "." FRANK_VERSION "${FRANK_VERSION_RAW}")

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
add_subdirectory(drivers/usbhid)


# Create a library for other drivers
add_library(drivers
    drivers/HDMI.c
    drivers/psram_init.c
    drivers/psram_allocator.c
)
target_include_directories(drivers PUBLIC drivers)
target_compile_definitions(drivers PRIVATE
    BOARD_${BOARD_VARIANT}
    PSRAM_MAX_FREQ_MHZ=${PSRAM_SPEED}
)
target_compile_options(drivers PRIVATE -Ofast)
target_link_libraries(drivers pico_stdlib hardware_dma hardware_pio hardware_spi)

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
    sdcard
    ps2kbd
    ps2mouse
    usbhid
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
    -Wl,--wrap=fseek
    -Wl,--wrap=ftell
    -Wl,--wrap=remove
    -Wl,--wrap=rename
)

# USB stdio
if(USB_HID_ENABLED)
    pico_enable_stdio_usb(frank-idtech1 0)
    pico_enable_stdio_uart(frank-idtech1 1)
else()
    pico_enable_stdio_usb(frank-idtech1 1)
    pico_enable_stdio_uart(frank-idtech1 0)
endif()

pico_add_extra_outputs(frank-idtech1)
