#pragma once
#include "compiler_types.hpp"
#include <filesystem>
bool buildSymbolTableFromTokenFile(const std::filesystem::path&, const std::filesystem::path&);
const SymbolEntry* getSymbolTable();
int getSymbolCount();
int getSymbolTableErrorCount();
int findSymbolByName(const std::string& name);
int findLiteralByValue(int value);
int appendTempSymbol();
