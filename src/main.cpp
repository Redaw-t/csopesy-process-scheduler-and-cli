// Entry point of the CSOPESY emulator.
#include <iostream>

#include "console/Console.h"

int main() {
    Console console(std::cin, std::cout);
    return console.run();
}
