#pragma once
#include "core/Document.h"
#include <QFont>
#include <QSizeF>
#include <QString>
#include <string_view>

namespace compositor::editing {
enum class TextAlignment { Left, Center, Right };
// QString positions are UTF-16 code units, matching the upstream project format
// and Qt's text cursor/layout APIs. Future attributed ranges use those positions.
struct TextStyle {
    QString content{"Text"},fontName{"Arial"};
    double fontSize{72},red{},green{},blue{},tracking{},leading{};
    TextAlignment alignment{TextAlignment::Left};
    std::optional<QSizeF> boxSize;
    static constexpr double padding=12;
    double lineHeight() const {return leading>0?leading:fontSize*1.2;}
    bool valid() const;
    bool operator==(const TextStyle&) const = default;
};
TextStyle decodeTextStyle(std::string_view);
std::string encodeTextStyle(const TextStyle&);
QFont textFont(const TextStyle&);
QSizeF textBoxSize(const TextStyle&);
double textFirstBaseline(const TextStyle&);
std::shared_ptr<const Raster> textRaster(const TextStyle&);
std::string textLayerName(const QString&);
Layer createTextLayer(Point origin,const TextStyle&);
// Changes the source while keeping its transformed top-left, independent X/Y
// scale, rotation, flips, effects, clipping and explicitly placed mask intact.
Layer restyleText(const Layer&,const TextStyle&);
}
