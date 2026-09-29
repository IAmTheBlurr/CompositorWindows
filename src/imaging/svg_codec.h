#pragma once
#include "image_types.h"
#include <filesystem>
#include <optional>
#include <utility>
namespace compositor::imaging {
DecodedImage decodeSvg(const std::filesystem::path&,std::optional<std::pair<int,int>> fitting={},const ImportOptions& = {});
}
