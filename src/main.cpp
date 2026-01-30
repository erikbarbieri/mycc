#include <fstream>
#include <iostream>
#include <string>

int main()
{
    const std::string path = "tests\\test_code.txt";
    std::ifstream file(path);

    if (!file.is_open())
    {
        std::cerr << "Failed to open: " << path << "\n";
        return 1;
    }

    char character;
    // loop through the file character by character until EOF
    while(file.get(character))
    {
        std::cout << character << "\n";
    }
}
