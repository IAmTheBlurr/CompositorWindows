#pragma once
#include "imaging/image_types.h"
#include <QWidget>
#include <filesystem>
#include <optional>
namespace compositor::ui {
std::optional<imaging::DecodedImage> developRawImage(QWidget*,const std::filesystem::path&,const imaging::ImportOptions& = {});
}
