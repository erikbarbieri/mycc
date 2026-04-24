#pragma once

#include <filesystem>

bool generateSourceAndCodeListings(const std::filesystem::path& sourceInputPath,
                                   const std::filesystem::path& assemblyInputPath,
                                   const std::filesystem::path& sourceListingOutputPath,
                                   const std::filesystem::path& codeListingOutputPath);

int getListingGeneratorErrorCount();
