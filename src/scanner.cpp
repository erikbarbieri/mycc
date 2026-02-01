#include <fstream>
#include <iostream>
#include <string>
#include <filesystem>

bool scanFile(const std::filesystem::path &fileInputPath, const std::filesystem::path &fileOutputPath)
{
    std::ifstream inputFile(fileInputPath);
    std::ofstream outputFile(fileOutputPath);

    if (!inputFile.is_open())
    {
        std::cerr << "Failed to open: " << fileInputPath << "\n";
        return false;
    }

    char character;
    // loop through the file character by character until EOF
    while (inputFile.get(character))
    {
        outputFile << character << "\n";
    }

    // scan was successful
    return true;
}