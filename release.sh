#!/bin/bash
#
# release.sh - Build release variants of frank-idtech1
#
# Creates UF2 files for each board variant (M1, M2, Z0).
# M1/M2: 504 MHz CPU, 100 MHz PSRAM, 66 MHz Flash.
# Z0 (Waveshare RP2350-PiZero): 504 MHz CPU, 133 MHz PSRAM, 66 MHz Flash,
# USB keyboard/mouse on its "PIO-USB" socket.
# USB HID and PS/2 keyboard/mouse supported.
#
# Output format: frank-idtech1_B_A_BB.uf2
#   B  = Board variant (m1, m2 or z0)
#   A  = Major version
#   BB = Minor version (zero-padded)
#

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# Version file
VERSION_FILE="version.txt"

# Read last version or initialize
if [[ -f "$VERSION_FILE" ]]; then
    read -r LAST_MAJOR LAST_MINOR < "$VERSION_FILE"
else
    LAST_MAJOR=1
    LAST_MINOR=0
fi

# Calculate next version (for default suggestion)
NEXT_MINOR=$((LAST_MINOR + 1))
NEXT_MAJOR=$LAST_MAJOR
if [[ $NEXT_MINOR -ge 100 ]]; then
    NEXT_MAJOR=$((NEXT_MAJOR + 1))
    NEXT_MINOR=0
fi

# Version input: from command line arg or interactive prompt
echo ""
echo -e "${CYAN}┌─────────────────────────────────────────────────────────────────┐${NC}"
echo -e "${CYAN}│                    frank-idtech1 Release Builder                  │${NC}"
echo -e "${CYAN}└─────────────────────────────────────────────────────────────────┘${NC}"
echo ""
echo -e "Last version: ${YELLOW}${LAST_MAJOR}.$(printf '%02d' $LAST_MINOR)${NC}"
echo ""

DEFAULT_VERSION="${NEXT_MAJOR}.$(printf '%02d' $NEXT_MINOR)"
if [[ -n "$1" ]]; then
    INPUT_VERSION="$1"
    echo -e "Version from command line: ${GREEN}${INPUT_VERSION}${NC}"
else
    read -p "Enter version [default: $DEFAULT_VERSION]: " INPUT_VERSION
    INPUT_VERSION=${INPUT_VERSION:-$DEFAULT_VERSION}
fi

# Parse version (handle both "1.00" and "1 00" formats)
if [[ "$INPUT_VERSION" == *"."* ]]; then
    MAJOR="${INPUT_VERSION%%.*}"
    MINOR="${INPUT_VERSION##*.}"
else
    read -r MAJOR MINOR <<< "$INPUT_VERSION"
fi

# Remove leading zeros for arithmetic, then re-pad
MINOR=$((10#$MINOR))
MAJOR=$((10#$MAJOR))

# Validate
if [[ $MAJOR -lt 1 ]]; then
    echo -e "${RED}Error: Major version must be >= 1${NC}"
    exit 1
fi
if [[ $MINOR -lt 0 || $MINOR -ge 100 ]]; then
    echo -e "${RED}Error: Minor version must be 0-99${NC}"
    exit 1
fi

# Format version string
VERSION="${MAJOR}_$(printf '%02d' $MINOR)"
echo ""
echo -e "${GREEN}Building release version: ${MAJOR}.$(printf '%02d' $MINOR)${NC}"

# Save new version
echo "$MAJOR $MINOR" > "$VERSION_FILE"

# Create release directory
RELEASE_DIR="$SCRIPT_DIR/release"
mkdir -p "$RELEASE_DIR"

# Build configurations: "BOARD CPU_SPEED PSRAM_SPEED FLASH_SPEED"
CONFIGS=(
    "M1 504 100 66"
    "M2 504 100 66"
    "Z0 504 133 66"
)

BUILD_COUNT=0
TOTAL_BUILDS=${#CONFIGS[@]}

echo ""
echo -e "${YELLOW}Building $TOTAL_BUILDS firmware variants...${NC}"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

for config in "${CONFIGS[@]}"; do
    read -r BOARD CPU PSRAM FLASH <<< "$config"

    BUILD_COUNT=$((BUILD_COUNT + 1))

    # Output filename
    BOARD_LC=$(echo "$BOARD" | tr '[:upper:]' '[:lower:]')
    OUTPUT_NAME="frank-idtech1_${BOARD_LC}_${VERSION}.uf2"

    echo ""
    echo -e "${CYAN}[$BUILD_COUNT/$TOTAL_BUILDS] Building: $OUTPUT_NAME${NC}"
    echo -e "  Board: $BOARD | CPU: ${CPU} MHz | PSRAM: ${PSRAM} MHz | Flash: ${FLASH} MHz"

    # Clean and create build directory
    rm -rf build
    mkdir build
    cd build

    # Configure with CMake (USB HID enabled for keyboard/mouse support)
    cmake .. -DBOARD_VARIANT="$BOARD" -DCPU_SPEED="$CPU" -DPSRAM_SPEED="$PSRAM" -DFLASH_SPEED="$FLASH" -DUSB_HID_ENABLED=1 > /dev/null 2>&1

    # Build
    if make -j8 > /dev/null 2>&1; then
        # Copy UF2 to release directory
        if [[ -f "frank-idtech1.uf2" ]]; then
            cp "frank-idtech1.uf2" "$RELEASE_DIR/$OUTPUT_NAME"
            echo -e "  ${GREEN}✓ Success${NC} → release/$OUTPUT_NAME"
        else
            echo -e "  ${RED}✗ UF2 not found${NC}"
        fi
    else
        echo -e "  ${RED}✗ Build failed${NC}"
    fi

    cd "$SCRIPT_DIR"
done

# Clean up build directory
rm -rf build

echo ""
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo -e "${GREEN}Release build complete!${NC}"
echo ""
echo "Release files in: $RELEASE_DIR/"
echo ""
ls -la "$RELEASE_DIR"/frank-idtech1_*_${VERSION}.uf2 2>/dev/null | awk '{print "  " $9 " (" $5 " bytes)"}'
echo ""
echo -e "Version: ${CYAN}${MAJOR}.$(printf '%02d' $MINOR)${NC}"
