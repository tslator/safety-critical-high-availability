#include <iostream>
#include <string_view>

#ifndef SAFETY_CRIT_COMPILER_NAME
#define SAFETY_CRIT_COMPILER_NAME "Unknown Compiler"
#endif

int main(int argc, char* argv[]) {
    if (argc != 2 || std::string_view(argv[1]) != "--version") {
        std::cerr << "usage: safety-critical-ha --version\n";
        return 1;
    }
    std::cout << "safety-critical-ha version 0.1.0\n";
    std::cout << "Compiler: " << SAFETY_CRIT_COMPILER_NAME << "\n";
    return 0;
}
