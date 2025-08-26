#!/usr/bin/env bash

OS_CHECK=$(uname)
BUILD_NAME="cube_thingy"
BUILD_DIR="out"
ROM_NAME="cube_thingy.gen"

# A function to run the standard build process.
run_build() {
  echo "-----------------------------------"
  echo "  Starting build..."
  echo "-----------------------------------"

  # Conditional temp fixes for Linux on ARM architecture.
  if [ "$OS_CHECK" == "Linux" ]
  then
    if [ "$(uname -m)" == "aarch64" ]
    then
      # temp fixes for backend.c
      sed -i 's/dest = dest/dest = (unsigned int *)dest/' backend.c
      sed -i 's/native = get_native/native = (unsigned int*)get_native/' backend.c
    fi
  fi

  # Use parallel make jobs based on the operating system.
  if [ "$OS_CHECK" == "Darwin" ]
  then
    echo ""
    echo "-------------------------------"
    echo "   macOS build takes forever"
    echo "-------------------------------"
    echo ""
    # Use sysctl to get the number of CPU cores on macOS.
    make -j$(sysctl -n hw.ncpu)
  else
    # Use nproc to get the number of CPU cores on Linux.
    make -j$(nproc)
  fi

  # Check if the build was successful.
  if [ ! -f $BUILD_NAME ]
  then
    echo "$BUILD_NAME build failed?"
    exit 1
  fi

  if [ -d $BUILD_DIR ]
  then
    rm -rf $BUILD_DIR
  fi
  mkdir -p $BUILD_DIR
  if [ -f $ROM_NAME ]
  then
    cp $ROM_NAME ${BUILD_DIR}/
  else
    cp testrom.bin ${BUILD_DIR}/$ROM_NAME
  fi
  cp -r $BUILD_NAME cube_thingy.ttf gamecontrollerdb.txt rom.db default.cfg images shaders $BUILD_DIR/
  echo ""
  echo "-----------------------------------"
  echo "$BUILD_NAME built successfully to:"
  echo "$BUILD_DIR"
  echo "-----------------------------------"
}

# A function to run the clean command.
run_clean() {
  make clean
}

# A function to display the help message.
show_help() {
  echo "Usage: ./build.sh [option]"
  echo ""
  echo "Options:"
  echo "  -c    Clean the build directory"
  echo "  -h    Show this help message"
  echo "  (none)  Run the standard build process"
}

# The main function to parse and handle arguments.
parse_arguments() {
  case "$1" in
    "-c")
      run_clean
      run_build
      exit
      ;;
    "-h")
      show_help
      exit
      ;;
    *)
      run_build
      exit
      ;;
  esac
}

# Call the main argument parsing function with all provided arguments.
parse_arguments "$@"
