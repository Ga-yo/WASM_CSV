#include <emscripten/bind.h>
#include "csv_converter.h"

// =================================================================================
// Emscripten Bindings
// =================================================================================
// This block exposes the C++ functions to JavaScript.

EMSCRIPTEN_BINDINGS(csv_converter_bindings) {
    emscripten::function("convertToJsonOptimized", &convertToJsonOptimized);
}