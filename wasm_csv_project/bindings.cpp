#include <emscripten/bind.h>
#include "csv_converter.h"

// =================================================================================
// Emscripten Bindings
// =================================================================================
// This block exposes the C++ functions to JavaScript.

EMSCRIPTEN_BINDINGS(csv_converter_bindings) {
    // The 'function' from emscripten::bind can conflict with other libraries,
    // so we use the fully qualified name.
    emscripten::function("convertToJson", &convertToJson);
    emscripten::function("convertToJsonMetadataOnly", &convertToJsonMetadataOnly);
    emscripten::function("convertToJsonAuto", &convertToJsonAuto);
    emscripten::function("convertToJsonOptimized", &convertToJsonOptimized);
}