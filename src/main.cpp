#include <fstream>
#include <iostream>
#include <string>
#include <filesystem>
#include "scanner.hpp"
#include "parser.hpp"
#include "semantic_analzyer.hpp"
#include "code_generator.hpp"

int main()
{
    // this is where output files live, whole folder is ignored in gitignore
    std::filesystem::create_directories(std::filesystem::current_path() / "tests/outputs");

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
