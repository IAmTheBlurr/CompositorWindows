#include "Selection.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <mutex>
#include <stdexcept>

namespace compositor::editing {
namespace {
// Three normalized box passes approximate a Gaussian of sigma feather/2.
// Tiles include the entire convolution support, keeping seams independent of order.
class FeatherCoverage final:public GrayRasterSource {
    std::shared_ptr<const GrayRaster> source_;
    std::shared_ptr<const SelectionOutline> outline_;
    std::array<int,3> radii_;
    std::vector<double> exactWeights_;
    int halo_{};
    GrayBounds support_;
    mutable std::mutex mutex_;
    mutable std::map<std::pair<int,int>,std::vector<uint8_t>> tiles_;
public:
    FeatherCoverage(std::shared_ptr<const GrayRaster> source,double feather,std::shared_ptr<const SelectionOutline> outline):source_(std::move(source)),outline_(std::move(outline)){
        const double sigma=feather/2;int width=int(std::floor(std::sqrt(4*sigma*sigma+1)));if(!(width&1))--width;width=std::max(1,width);const int large=width+2;
        const int small=std::clamp(int(std::round((12*sigma*sigma-3*width*width-12*width-9)/(-4.*width-4))),0,3);
        for(int i=0;i<3;++i){radii_[i]=((i<small?width:large)-1)/2;halo_+=radii_[i];}
        if(feather<=10){halo_=int(std::ceil(feather*2));double sum=0;for(int i=-halo_;i<=halo_;++i){double weight=std::exp(-double(i)*i/(2*sigma*sigma));exactWeights_.push_back(weight);sum+=weight;}for(auto&weight:exactWeights_)weight/=sum;}
        auto b=source_->nonzeroBounds();if(b.empty())return;int left=std::max(0,b.x-halo_),top=std::max(0,b.y-halo_);support_={left,top,std::min(source_->width,b.x+b.width+halo_)-left,std::min(source_->height,b.y+b.height+halo_)-top};
    }
    uint8_t pixel(int x,int y)const override{
        if(x<support_.x||y<support_.y||x>=support_.x+support_.width||y>=support_.y+support_.height)return 0;
        std::lock_guard guard(mutex_);const auto key=std::pair{x/256,y/256};auto found=tiles_.find(key);
        if(found==tiles_.end()){
            const int side=256+halo_*2,originX=key.first*256-halo_,originY=key.second*256-halo_;std::vector<float> pixels(size_t(side)*side),temp(pixels.size());
            for(int py=0;py<side;++py)for(int px=0;px<side;++px)pixels[size_t(py)*side+px]=source_->pixel(std::clamp(originX+px,0,source_->width-1),std::clamp(originY+py,0,source_->height-1));
            if(!exactWeights_.empty()){
                for(int py=0;py<side;++py)for(int px=0;px<side;++px){double sum=0;for(int k=-halo_;k<=halo_;++k)sum+=pixels[size_t(py)*side+std::clamp(px+k,0,side-1)]*exactWeights_[size_t(k+halo_)];temp[size_t(py)*side+px]=float(sum);}
                for(int py=0;py<side;++py)for(int px=0;px<side;++px){double sum=0;for(int k=-halo_;k<=halo_;++k)sum+=temp[size_t(std::clamp(py+k,0,side-1))*side+px]*exactWeights_[size_t(k+halo_)];pixels[size_t(py)*side+px]=float(sum);}
            }
            else for(int radius:radii_){if(!radius)continue;const double divisor=radius*2+1;
                for(int py=0;py<side;++py){double sum=0;for(int k=-radius;k<=radius;++k)sum+=pixels[size_t(py)*side+std::clamp(k,0,side-1)];for(int px=0;px<side;++px){temp[size_t(py)*side+px]=float(sum/divisor);sum+=pixels[size_t(py)*side+std::min(side-1,px+radius+1)]-pixels[size_t(py)*side+std::max(0,px-radius)];}}
                for(int px=0;px<side;++px){double sum=0;for(int k=-radius;k<=radius;++k)sum+=temp[size_t(std::clamp(k,0,side-1))*side+px];for(int py=0;py<side;++py){pixels[size_t(py)*side+px]=float(sum/divisor);sum+=temp[size_t(std::min(side-1,py+radius+1))*side+px]-temp[size_t(std::max(0,py-radius))*side+px];}}
            }
            std::vector<uint8_t> tile(256*256);for(int py=0;py<256;++py)for(int px=0;px<256;++px)tile[size_t(py)*256+px]=uint8_t(std::clamp(std::lround(pixels[size_t(py+halo_)*side+px+halo_]),0L,255L));if(tiles_.size()>=32)tiles_.erase(tiles_.begin());found=tiles_.emplace(key,std::move(tile)).first;
        }
        return found->second[size_t(y%256)*256+x%256];
    }
    GrayBounds nonzeroBounds()const override{return support_;}
    size_t retainedBytes()const override{return source_->retainedBytes()+32*256*256;}
    std::shared_ptr<const SelectionOutline> vectorOutline()const override{return outline_;}
};
}
std::shared_ptr<const GrayRaster> featherCoverage(std::shared_ptr<const GrayRaster> source,double feather,std::shared_ptr<const SelectionOutline> outline){
    if(!source||!source->validStorage()||!std::isfinite(feather)||feather<0||feather>250)throw std::invalid_argument("Invalid selection feather");if(feather==0)return source;
    auto result=std::make_shared<GrayRaster>();result->width=source->width;result->height=source->height;result->source=std::make_shared<FeatherCoverage>(std::move(source),feather,std::move(outline));return result;
}
}
