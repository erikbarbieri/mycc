#include <fstream>
#include <iostream>
#include <string>
#include <filesystem>
#include "scanner.hpp"

int main()
{
    const std::string inputFileName = "test_code.txt";
    const std::string scannerFileName = "scanner_output.txt";
    const std::filesystem::path inputFilePath =
        std::filesystem::current_path() / "tests" / "inputs" / inputFileName;
    const std::filesystem::path outputFilePath =
        std::filesystem::current_path() / "tests" / "outputs" / scannerFileName;

    std::cout << "Scanning: " << inputFileName << " -> " << scannerFileName << "\n";
    if (!scanFile(inputFilePath, outputFilePath))
    {
        std::cerr << "Scanning failed.\n";
    }
    std::cout << "Scanning success!\n";
}
