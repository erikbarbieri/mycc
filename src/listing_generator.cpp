#include "listing_generator.hpp"

#include <fstream>
#include <iostream>
#include <string>

static int g_listingGeneratorErrorCount = 0;

int getListingGeneratorErrorCount() {
    return g_listingGeneratorErrorCount;
}

static void reportListingError(const std::string& message) {
    std::cerr << "Listing generator error: " << message << "\n";
    g_listingGeneratorErrorCount++;
}

static bool writeNumberedListing(const std::filesystem::path& inputPath,
                                 const std::filesystem::path& outputPath,
                                 const std::string& header) {
    std::ifstream input(inputPath);
    if (!input.is_open()) {
        reportListingError("failed to open input file: " + inputPath.string());
        return false;
    }

    std::ofstream output(outputPath);
    if (!output.is_open()) {
        reportListingError("failed to open output file: " + outputPath.string());
        return false;
    }

    output << header << "\n";

    std::string line;
    int lineNumber = 1;
    while (std::getline(input, line)) {
        output << lineNumber << '\t' << line << '\n';
        lineNumber++;
    }

    return true;
}

bool generateSourceAndCodeListings(const std::filesystem::path& sourceInputPath,
                                   const std::filesystem::path& assemblyInputPath,
                                   const std::filesystem::path& sourceListingOutputPath,
                                   const std::filesystem::path& codeListingOutputPath) {
    g_listingGeneratorErrorCount = 0;

    if (!writeNumberedListing(sourceInputPath, sourceListingOutputPath, "Line\tSource")) {
        return false;
    }

    if (!writeNumberedListing(assemblyInputPath, codeListingOutputPath, "Line\tAssembly")) {
        return false;
    }

    return (g_listingGeneratorErrorCount == 0);
}
