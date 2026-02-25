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

    int character;
    // loop through the file character by character until EOF
    while ((character = inputFile.get()) != EOF)
    {
        char c = static_cast<char>(character);
        outputFile << c << "\n";
    }

    // scan was successful
    return true;
}