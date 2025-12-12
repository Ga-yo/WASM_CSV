#include <emscripten/bind.h>
#include "csv_converter.h"

// =================================================================================
// Emscripten Bindings
// =================================================================================
// This block exposes the C++ functions to JavaScript.

// Bindings have been moved to csv_lib/csv_converter.cpp to support WasmCSVParser class
// EMSCRIPTEN_BINDINGS(csv_converter_bindings) {
//     emscripten::function("convertToJsonOptimized", &convertToJsonOptimized);
// }