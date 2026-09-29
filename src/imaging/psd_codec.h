#pragma once
#include "core/Document.h"
#include "image_types.h"
#include <filesystem>
#include <span>
#include <QStringList>
namespace compositor::imaging {
struct PhotoshopImport {Document document;QStringList conversions;};
PhotoshopImport decodePhotoshop(const std::filesystem::path&,const ImportOptions& = {});
PhotoshopImport decodePhotoshop(std::span<const uint8_t>,const ImportOptions& = {});
}
