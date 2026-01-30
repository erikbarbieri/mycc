#include <fstream>
#include <iostream>
#include <string>
#include <filesystem>

int main()
{
    const std::filesystem::path path =
        std::filesystem::current_path() / "tests" / "test_code.txt";
    std::ifstream file(path);

    if (!file.is_open())
    {
        std::cerr << "Failed to open: " << path << "\n";
        return 1;
    }

    char character;
    // loop through the file character by character until EOF
    while (file.get(character))
    {
        std::cout << character << "\n";
    }
}
