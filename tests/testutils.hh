// This Source Code Form is licensed MPL-2.0: http://mozilla.org/MPL/2.0

#pragma once

#include <filesystem>
#include <random>
#include <stdexcept>
#include <string>

namespace LiquidSFZTest
{

// The caller owns the directory and is responsible for removing it.
inline std::filesystem::path
create_temp_directory (const std::string& prefix)
{
  namespace fs = std::filesystem;
  const auto temp = fs::temp_directory_path();
  std::random_device random;
  for (int attempt = 0; attempt < 100; attempt++)
    {
      const auto path = temp / (prefix + std::to_string (random()));
      std::error_code ec;
      if (fs::create_directory (path, ec))
        return path;
      // An existing file or directory is a collision; retry with a new name.
      if (ec && ec != std::errc::file_exists)
        throw fs::filesystem_error ("create test directory", path, ec);
    }
  throw std::runtime_error ("unable to create unique test directory");
}

}
