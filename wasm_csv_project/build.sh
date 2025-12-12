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
BUILD_TYPE="release"
if [ "$1" == "debug" ] || [ "$1" == "-d" ]; then
    BUILD_TYPE="debug"
fi

# Clean previous build files
echo ""
echo "Cleaning previous build files..."
rm -f csv_converter.js csv_converter.wasm index.js index.wasm
echo "✓ Cleaned"

# Source files to compile
SOURCE_FILES=(
    csv_lib/csv_converter.cpp
    csv_lib/type_checker.cpp
    csv_lib/csv_utils.cpp
    csv_lib/csv_parser.cpp
    bindings.cpp
)

# Common flags for both builds
COMMON_FLAGS=(
    -s WASM=1
    -s MODULARIZE=0
    -s EXPORT_ES6=0
    -s ENVIRONMENT='web'
    -s ALLOW_MEMORY_GROWTH=1
    -s MAXIMUM_MEMORY=4GB
    -s INITIAL_MEMORY=256MB
    -s STACK_SIZE=16MB
    -s EXPORTED_RUNTIME_METHODS='["ccall","cwrap"]'
    -s NO_EXIT_RUNTIME=1
    -s DISABLE_EXCEPTION_CATCHING=0
    -Icsv_lib # Add include path for csv_lib directory
    --bind
    -std=c++17
)

# Build release version with maximum performance optimizations
build_release() {
    echo ""
    echo "Building RELEASE version with MAXIMUM PERFORMANCE optimizations..."
    echo "Applying aggressive optimization flags..."
    echo ""

    emcc "${SOURCE_FILES[@]}" \
        -o csv_converter.js \
        "${COMMON_FLAGS[@]}" \
        -O3 \
        -flto \
        -msimd128 \
        -fomit-frame-pointer \
        -finline-functions \
        -funroll-loops \
        -s AGGRESSIVE_VARIABLE_ELIMINATION=1 \
        -s ELIMINATE_DUPLICATE_FUNCTIONS=1 \
        -s IGNORE_CLOSURE_COMPILER_ERRORS=1 \
        --closure 1

    if [ $? -eq 0 ]; then
        echo "✓ Release build successful"
        echo ""
        echo "Performance optimizations applied:"
        echo "  • Link-Time Optimization (LTO level 3)"
        echo "  • SIMD vectorization enabled"
        echo "  • Aggressive function inlining"
        echo "  • Loop unrolling"
        echo "  • Dead code elimination"
        echo "  • Closure compiler optimization"
        echo ""
        echo "Available functions:"
        echo "  • convertToJson() - Standard conversion"
        echo "  • convertToJsonAuto() - Auto-select based on size"
        echo "  • convertToJsonOptimized() - Optimized algorithm"
        return 0
    else
        echo "✗ Release build failed with closure compiler"
        echo "Trying without closure compiler..."
        emcc "${SOURCE_FILES[@]}" \
            -o csv_converter.js \
            "${COMMON_FLAGS[@]}" \
            -O3 \
            -flto \
            -msimd128 \
            -fomit-frame-pointer \
            -finline-functions \
            -funroll-loops \
            -s AGGRESSIVE_VARIABLE_ELIMINATION=1

        if [ $? -eq 0 ]; then
            echo "✓ Release build successful (without closure)"
            return 0
        else
            echo "✗ Build failed"
            return 1
        fi
    fi
}

# Build debug version
build_debug() {
    echo ""
    echo "Building debug version..."
    emcc "${SOURCE_FILES[@]}" \
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
    "release")
        build_release
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
echo "  ./build.sh        # Build release version (maximum performance optimizations)"
echo "  ./build.sh debug  # Build debug version (for development)"
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
