#pragma once
#include "image_types.h"
#include <filesystem>
#include <memory>
namespace compositor::imaging {
struct RawDevelopSettings {
    double exposure{},temperature{5000},tint{},boost{1};
    bool asShot{true};
};
class RawFrame {
public:
    explicit RawFrame(const std::filesystem::path&,const ImportOptions& = {});
    ~RawFrame();
    RawFrame(const RawFrame&)=delete;
    RawFrame& operator=(const RawFrame&)=delete;
    RawDevelopSettings settings()const;
    DecodedImage develop(const RawDevelopSettings&,bool preview=false,const ImportOptions& = {});
    static bool matches(const std::filesystem::path&);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
