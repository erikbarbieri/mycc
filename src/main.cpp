#include <fstream>
#include <iostream>
#include <string>

int main()
{
    const std::string path = "tests\\test_code.txt";
    std::ifstream file(path, std::ios::binary);

    if (!file.is_open())
    {
        std::cerr << "Failed to open: " << path << "\n";
        return 1;
    }

    while(!file.eof())
    {
        // get() returns an int, not a char
        char character = static_cast<char>(file.get());
        
        std::cout << character << "\n";
    }

    if (!file.eof())
    {
        std::cerr << "Read error occurred\n";
        return 2;
    }
}
