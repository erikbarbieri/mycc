#pragma once
#include "compiler_types.hpp"
#include <filesystem>
bool scanFile(const std::filesystem::path&, const std::filesystem::path&);
const Token* getTokenList();
int getTokenCount();
int getScannerErrorCount();
