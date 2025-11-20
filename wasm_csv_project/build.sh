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
if [ "$1" == "optimized" ] || [ "$1" == "-o" ]; then
    BUILD_TYPE="optimized"
elif [ "$1" == "both" ] || [ "$1" == "-b" ]; then
    BUILD_TYPE="both"
fi

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

# Build standard version
build_standard() {
    echo ""
    echo "Building standard version..."
    emcc csv_converter.cpp \
        -o csv_converter.js \
        "${COMMON_FLAGS[@]}" \
        -O2

    if [ $? -eq 0 ]; then
        echo "✓ Standard build successful"
        return 0
    else
        echo "✗ Standard build failed"
        return 1
    fi
}

# Build optimized version for large files
build_optimized() {
    echo ""
    echo "Building optimized version for large files..."
    emcc csv_converter_optimized.cpp \
        -o csv_converter.js \
        "${COMMON_FLAGS[@]}" \
        -O3 \
        -flto \
        -s AGGRESSIVE_VARIABLE_ELIMINATION=1 \
        -s DISABLE_EXCEPTION_CATCHING=0 \
        --closure 1

    if [ $? -eq 0 ]; then
        echo "✓ Optimized build successful"
        return 0
    else
        echo "✗ Optimized build failed"
        return 1
    fi
}

# Execute builds
case $BUILD_TYPE in
    "standard")
        build_standard
        ;;
    "optimized")
        build_optimized
        ;;
    "both")
        build_standard
        if [ $? -eq 0 ]; then
            mv csv_converter.js csv_converter_standard.js
            mv csv_converter.wasm csv_converter_standard.wasm
        fi
        build_optimized
        if [ $? -eq 0 ]; then
            mv csv_converter.js csv_converter_optimized.js
            mv csv_converter.wasm csv_converter_optimized.wasm
        fi
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
echo "  ./build.sh           # Build standard version"
echo "  ./build.sh optimized # Build optimized version (for large files)"
echo "  ./build.sh both      # Build both versions"
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
