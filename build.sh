#!/usr/bin/env bash
#
# Build script for BlastEm raw binary distribution (with GDB remote & embedded font support).
# Assembles the compiled binary and required runtime files in the 'blastem-bin' directory.
#
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT_DIR"

OUT_DIR="blastem-bin"

# Help message
if [ "${1:-}" = "-h" ] || [ "${1:-}" = "--help" ]; then
    echo "Usage: $0 [options]"
    echo ""
    echo "Options:"
    echo "  --clean       Remove build objects and the blastem-bin directory"
    echo "  --rebuild     Perform a clean build from scratch"
    echo "  --install     Install binary and assets to ~/.local/bin (or custom path)"
    echo "  -h, --help    Show this help message"
    echo ""
    echo "Environment variables:"
    echo "  CC            C compiler (default: clang on macOS / system cc)"
    echo "  JOBS          Number of parallel jobs (default: auto-detected CPU count)"
    exit 0
fi

# Clean target
if [ "${1:-}" = "clean" ] || [ "${1:-}" = "--clean" ]; then
    echo "Cleaning build artifacts..."
    make clean 2>/dev/null || true
    rm -rf "$OUT_DIR"
    echo "Clean complete."
    exit 0
fi

# Rebuild target
if [ "${1:-}" = "rebuild" ] || [ "${1:-}" = "--rebuild" ]; then
    echo "Cleaning before rebuild..."
    make clean 2>/dev/null || true
    rm -rf "$OUT_DIR"
fi

# Install target
if [ "${1:-}" = "install" ] || [ "${1:-}" = "--install" ]; then
    INSTALL_DIR="${2:-$HOME/.local/bin}"
    if [ ! -d "$OUT_DIR" ] || [ ! -f "$OUT_DIR/blastem" ]; then
        echo "Building BlastEm first..."
        "$0"
    fi
    echo "Installing BlastEm to $INSTALL_DIR..."
    mkdir -p "$INSTALL_DIR"
    cp -R "$OUT_DIR"/* "$INSTALL_DIR/"
    echo "Installed successfully to $INSTALL_DIR."
    exit 0
fi

# Verify dependencies
echo "==> Checking build prerequisites..."
command -v make >/dev/null 2>&1 || { echo "Error: 'make' not found." >&2; exit 1; }
command -v python3 >/dev/null 2>&1 || { echo "Error: 'python3' not found (required for CPU DSL / font conversion)." >&2; exit 1; }

PKG_CONFIG_CMD=""
if command -v pkg-config >/dev/null 2>&1; then
    PKG_CONFIG_CMD="pkg-config"
elif command -v pkgconf >/dev/null 2>&1; then
    PKG_CONFIG_CMD="pkgconf"
else
    echo "Error: pkg-config (or pkgconf) not found." >&2
    if [ "$(uname -s)" = "Darwin" ]; then
        echo "Install via Homebrew: brew install pkg-config" >&2
    fi
    exit 1
fi

if ! "$PKG_CONFIG_CMD" --exists sdl2 glew; then
    echo "Error: Required development libraries (sdl2, glew) not found via pkg-config." >&2
    if [ "$(uname -s)" = "Darwin" ]; then
        echo "Install via Homebrew: brew install sdl2 glew" >&2
    fi
    exit 1
fi

# Compiler and warning flags
# As referenced in .github/workflows/build.yml, modern clang / Apple clang
# promotes certain legacy C warnings to errors. We downgrade them here.
CWARN="-Wno-error=incompatible-pointer-types -Wno-error=incompatible-function-pointer-types -Wno-error=int-conversion -Wno-error=implicit-int"

if [ -z "${CC:-}" ]; then
    if [ "$(uname -s)" = "Darwin" ] && command -v clang >/dev/null 2>&1; then
        CC="clang $CWARN"
    else
        CC="cc $CWARN"
    fi
else
    # If custom CC is provided, ensure CWARN is included
    CC="$CC $CWARN"
fi

NPROC="${JOBS:-$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)}"

echo "==> Building BlastEm binary (jobs: $NPROC, CC: $CC)..."
make -j"$NPROC" CC="$CC" blastem

if [ ! -f "blastem" ]; then
    echo "Error: blastem binary was not generated." >&2
    exit 1
fi

echo "==> Assembling raw binary and required runtime files in '$OUT_DIR'..."
rm -rf "$OUT_DIR"
mkdir -p "$OUT_DIR"

# Copy the executable
cp blastem "$OUT_DIR/blastem"
chmod +x "$OUT_DIR/blastem"

# Copy required runtime files (configuration, shaders, controller mappings, ROM DB, licenses)
REQUIRED_FILES=(
    "default.cfg"
    "rom.db"
    "gamecontrollerdb.txt"
    "systems.cfg"
    "shaders"
    "images"
    "COPYING"
    "COPYING.fork"
)

for item in "${REQUIRED_FILES[@]}"; do
    if [ -e "$item" ]; then
        cp -R "$item" "$OUT_DIR/"
    else
        echo "Warning: Required file or directory '$item' not found!" >&2
    fi
done

# Ensure no object files or intermediate build files slipped into blastem-bin
find "$OUT_DIR" -type f \( -name "*.o" -o -name "*.d" -o -name "*.a" \) -delete

echo "==> Verifying binary..."
"$OUT_DIR/blastem" -v

echo ""
echo "Build successful! Contents of $OUT_DIR:"
ls -lh "$OUT_DIR"
