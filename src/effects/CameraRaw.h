#pragma once
#include "core/Document.h"
#include <array>
#include <functional>
#include <map>
#include <string>
#include <string_view>
namespace compositor::effects {
struct CameraRawParameter {const char* group;const char* key;const char* label;double minimum,maximum,initial;};
const std::vector<CameraRawParameter>& cameraRawParameters();
struct CameraRawPointColor {double hue{},saturation{},luminance{},hueShift{},saturationShift{},luminanceShift{},hueRange{30},saturationRange{.4},luminanceRange{.4};};
struct CameraRawSettings {
    std::map<std::string,double,std::less<>> values;
    std::array<std::vector<Point>,4> curves;
    std::vector<CameraRawPointColor> points;
    std::vector<std::pair<Point,Point>> guides;
    CameraRawSettings();
    double get(std::string_view key)const;
    void set(std::string_view key,double value);
    bool identity()const;
};
struct CameraRawPreview {int clipping{},pointColor{-1};bool sharpenMask{},shadowOverlay{},highlightOverlay{};};
std::shared_ptr<const Raster> applyCameraRaw(const Raster&,const CameraRawSettings&,double previewScale=1,CameraRawPreview={},std::function<bool()> cancelled={});
std::optional<Point> cameraRawWhiteBalance(const Raster&,std::optional<Point> sample={});
std::string encodeCameraRawSettings(const CameraRawSettings&);
CameraRawSettings decodeCameraRawSettings(std::string_view);
}
