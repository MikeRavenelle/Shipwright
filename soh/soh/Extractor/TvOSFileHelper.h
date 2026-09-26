#pragma once
#ifdef __TVOS__
#include <cstddef>
#include <string>

bool TvOSWriteFile(const std::string& destDir, const std::string& filename,
                   const char* data, std::size_t size);

#endif
