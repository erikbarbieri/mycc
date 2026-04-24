#pragma once
#include <filesystem>
bool generateAssemblyFromQuadAndSymbolFiles(const std::filesystem::path&,
                                            const std::filesystem::path&,
                                            const std::filesystem::path&,
                                            const std::filesystem::path&);
int getCodeGeneratorErrorCount();
