#include <filesystem>
#include <iostream>

#include "code_generator.hpp"
#include "listing_generator.hpp"
#include "parser.hpp"
#include "scanner.hpp"
#include "symbol_table.hpp"

// If I knew more Python, this would all be written in Python. Or Haskell.

// Compiler? I hardly know her!
int main() {
    const std::filesystem::path inputPath =
        std::filesystem::current_path() / "tests" / "inputs" / "test_code.txt";

    const std::filesystem::path tokenOutputPath =
        std::filesystem::current_path() / "tests" / "outputs" / "tokens.txt";

    const std::filesystem::path symbolOutputPath =
        std::filesystem::current_path() / "tests" / "outputs" / "symbols.txt";

    const std::filesystem::path quadOutputPath =
        std::filesystem::current_path() / "tests" / "outputs" / "quads.txt";

    const std::filesystem::path symbolWithTempsOutputPath =
        std::filesystem::current_path() / "tests" / "outputs" / "symbols_after_parse.txt";

    const std::filesystem::path asmOutputPath =
        std::filesystem::current_path() / "tests" / "outputs" / "program.asm";

    const std::filesystem::path numericCodeOutputPath =
        std::filesystem::current_path() / "tests" / "outputs" / "code_numeric.txt";

    const std::filesystem::path sourceListingOutputPath =
        std::filesystem::current_path() / "tests" / "outputs" / "source_listing.txt";

    const std::filesystem::path codeListingOutputPath =
        std::filesystem::current_path() / "tests" / "outputs" / "code_listing.txt";

    std::filesystem::create_directories(tokenOutputPath.parent_path());

    std::cout << "Input               : " << inputPath << "\n";
    std::cout << "Token output        : " << tokenOutputPath << "\n";
    std::cout << "Symbol output       : " << symbolOutputPath << "\n";
    std::cout << "Quad output         : " << quadOutputPath << "\n";
    std::cout << "Symbols + temps     : " << symbolWithTempsOutputPath << "\n";
    std::cout << "Assembly output     : " << asmOutputPath << "\n";
    std::cout << "Numeric code output : " << numericCodeOutputPath << "\n";
    std::cout << "Source listing      : " << sourceListingOutputPath << "\n";
    std::cout << "Code listing        : " << codeListingOutputPath << "\n";

    if (!scanFile(inputPath, tokenOutputPath)) {
        std::cerr << "Scanner pass failed.\n";
        return 1;
    }

    if (!buildSymbolTableFromTokenFile(tokenOutputPath, symbolOutputPath)) {
        std::cerr << "Symbol table pass completed with " << getSymbolTableErrorCount()
                  << " error(s).\n";
        return 1;
    }

    if (!buildQuadsFromTokenAndSymbolFiles(tokenOutputPath, symbolOutputPath, quadOutputPath,
                                           symbolWithTempsOutputPath)) {
        std::cerr << "Parser pass completed with " << getParserErrorCount() << " error(s).\n";
        return 1;
    }

    if (!generateAssemblyFromQuadAndSymbolFiles(quadOutputPath, symbolWithTempsOutputPath,
                                                asmOutputPath, numericCodeOutputPath)) {
        std::cerr << "Code generation pass completed with " << getCodeGeneratorErrorCount()
                  << " error(s).\n";
        return 1;
    }

    if (!generateSourceAndCodeListings(inputPath, asmOutputPath, sourceListingOutputPath,
                                       codeListingOutputPath)) {
        std::cerr << "Listing pass completed with " << getListingGeneratorErrorCount()
                  << " error(s).\n";
        return 1;
    }

    std::cout << "Success. " << getQuadCount() << " quads translated into assembly.\n";

    return 0;
}
