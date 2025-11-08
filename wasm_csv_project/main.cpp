#include <emscripten/emscripten.h>
#include <iostream>

int main() {
    std::cout << "Hello, WebAssembly (C++)!" << std::endl;

    EM_ASM({
        document.body.style.fontFamily = 'Arial, sans-serif';
        document.body.innerHTML = '<h1>Hello, WebAssembly (C++)!</h1><p>Printed from C++ via EM_ASM()</p>';
        console.log('Hello from EM_ASM!');
    });

    return 0;
}


