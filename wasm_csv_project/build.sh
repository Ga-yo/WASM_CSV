#!/bin/bash

# CSV to JSON WASM Converter Build Script
# Requires Emscripten SDK (emsdk) to be installed and activated

echo "=========================================="
echo "CSV to JSON WASM Converter Build Script"
echo "=========================================="

# Check if emcc is available
if ! command -v emcc &> /dev/null; then
    echo "Error: emcc (Emscripten compiler) not found."
    echo "Please install and activate Emscripten SDK:"
    echo "  git clone https://github.com/emscripten-core/emsdk.git"
    echo "  cd emsdk"
    echo "  ./emsdk install latest"
    echo "  ./emsdk activate latest"
    echo "  source ./emsdk_env.sh"
    exit 1
fi

# Parse arguments
BUILD_TYPE="standard"
if [ "$1" == "debug" ] || [ "$1" == "-d" ]; then
    BUILD_TYPE="debug"
fi

# Clean previous build files
echo ""
echo "Cleaning previous build files..."
rm -f csv_converter.js csv_converter.wasm index.js index.wasm
echo "✓ Cleaned"

# Common flags for both builds
COMMON_FLAGS=(
    -s WASM=1
    -s MODULARIZE=0
    -s EXPORT_ES6=0
    -s ENVIRONMENT='web'
    -s ALLOW_MEMORY_GROWTH=1
    -s MAXIMUM_MEMORY=2GB
    -s EXPORTED_RUNTIME_METHODS='["ccall","cwrap"]'
    -s NO_EXIT_RUNTIME=1
    --bind
    -std=c++17
)

# Build standard version (optimized)
build_standard() {
    echo ""
    echo "Building unified version..."
    emcc csv_converter.cpp \
        -o csv_converter.js \
        "${COMMON_FLAGS[@]}" \
        -O3 \
        -flto

    if [ $? -eq 0 ]; then
        echo "✓ Build successful"
        return 0
    else
        echo "✗ Build failed"
        return 1
    fi
}

# Build debug version
build_debug() {
    echo ""
    echo "Building debug version..."
    emcc csv_converter.cpp \
        -o csv_converter.js \
        "${COMMON_FLAGS[@]}" \
        -O0 \
        -g

    if [ $? -eq 0 ]; then
        echo "✓ Debug build successful"
        return 0
    else
        echo "✗ Debug build failed"
        return 1
    fi
}

# Execute builds
case $BUILD_TYPE in
    "standard")
        build_standard
        ;;
    "debug")
        build_debug
        ;;
esac

echo ""
echo "=========================================="
echo "Build complete!"
echo "=========================================="
echo ""
echo "Generated files:"
ls -lh *.js *.wasm 2>/dev/null | awk '{print "  " $9 " (" $5 ")"}'
echo ""
echo "Usage:"
echo "  ./build.sh        # Build optimized version"
echo "  ./build.sh debug  # Build debug version"
echo ""
echo "To test, start a local server:"
echo "  python3 -m http.server 8080"
echo "Then open: http://localhost:8080/index.html"
echo ""
echo "WASM Advantages for Large File Processing:"
echo "  • 10-100x faster than pure JavaScript"
echo "  • Efficient memory management"
echo "  • Streaming processing (low memory footprint)"
echo "  • SIMD support for parallel operations"
echo ""
