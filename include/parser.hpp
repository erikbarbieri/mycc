#pragma once

#include "compiler_types.hpp"
#include <filesystem>

bool buildQuadsFromTokenAndSymbolFiles(const std::filesystem::path& tokenInputPath,
                                       const std::filesystem::path& symbolInputPath,
                                       const std::filesystem::path& quadOutputPath,
                                       const std::filesystem::path& symbolWithTempsOutputPath);

const Quad* getQuadList();
int getQuadCount();
int getParserErrorCount();
