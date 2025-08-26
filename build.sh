#!/usr/bin/env bash

OS_CHECK=$(uname)

BUILD_NAME="cube_thingy"

make clean

if [ "$OS_CHECK" == "Linux" ]
then
  if [ "$(uname -m)" == "aarch64" ]
  then
    # temp fixes for backend.c
    sed -i 's/dest = dest/dest = (unsigned int *)dest/' backend.c
    sed -i 's/native = get_native/native = (unsigned int*)get_native/' backend.c
  fi
fi

if [ "$OS_CHECK" == "Darwin" ]
then
  echo ""
  echo "-------------------------------"
  echo "   macOS build takes forever"
  echo "-------------------------------"
  echo ""
  make -j$(sysctl -n hw.ncpu)
else
  make -j$(nproc)
fi

if [ ! -f $BUILD_NAME ]
then
  echo "$BUILD_NAME build failed?"
  exit
fi

echo ""
echo "-----------------------------------"
echo "$BUILD_NAME built"
echo "-----------------------------------"

