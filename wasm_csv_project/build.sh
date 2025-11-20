#!/bin/bash

# CSV to JSON WASM Converter Build Script
# Requires Emscripten SDK (emsdk) to be installed and activated

echo "Building CSV to JSON WASM Converter..."

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

# Compile C++ to WebAssembly
emcc csv_converter.cpp \
    -o csv_converter.js \
    -s WASM=1 \
    -s MODULARIZE=0 \
    -s EXPORT_ES6=0 \
    -s ENVIRONMENT='web' \
    -s ALLOW_MEMORY_GROWTH=1 \
    -s MAXIMUM_MEMORY=512MB \
    -s EXPORTED_RUNTIME_METHODS='["ccall","cwrap"]' \
    -s NO_EXIT_RUNTIME=1 \
    --bind \
    -O2 \
    -std=c++17

if [ $? -eq 0 ]; then
    echo "Build successful!"
    echo "Generated files:"
    echo "  - csv_converter.js"
    echo "  - csv_converter.wasm"
    echo ""
    echo "To test, start a local server:"
    echo "  python3 -m http.server 8080"
    echo "Then open: http://localhost:8080/index.html"
else
    echo "Build failed!"
    exit 1
fi
