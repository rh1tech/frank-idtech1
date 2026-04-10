#!/bin/bash
rm -rf ./build
mkdir build
cd build
cmake -DPICO_PLATFORM=rp2350 \
      -DBOARD_VARIANT=M2 \
      -DCPU_SPEED=504 \
      -DPSRAM_SPEED=133 \
      -DFLASH_SPEED=66 \
      "$@" ..
make -j4
