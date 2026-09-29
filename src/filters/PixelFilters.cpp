// Behavior translated from Compositor a19db9011282399785dc18efcfded904627bdcc2
// Filters.swift and ContentFill.swift. Copyright (c) 2026 Wonder Assembly LLC.
// MIT license retained in dependencies/imaging/notices/Compositor-MIT.txt.
#include "core/DocumentLimits.h"
#include "PixelFilters.h"
#include "graphics/PixelAlgorithms.h"
extern "C" {
#include "graphics/upstream/AdjustPixels.h"
}
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>
namespace compositor::filters {
namespace {
void cancel(const Limits& l){if(l.cancelled&&l.cancelled())throw std::runtime_error("Filter cancelled");}
std::size_t count(int w,int h,const Limits& l,std::uint64_t bytesPerPixel=24){
    if(w<1||h<1||w>30000||h>30000||std::uint64_t(w)*h>limits::surfacePixels)throw std::runtime_error("Filter exceeds the 200-megapixel or 30000-pixel-side budget");
    const auto n=std::uint64_t(w)*h;
    // Tiled intermediates also allocate full edge tiles, especially significant
    // for very thin images. Three grids can coexist during grow/run/final trim.
    const auto tiles=std::uint64_t((w+Raster::tileSide-1)/Raster::tileSide)*((h+Raster::tileSide-1)/Raster::tileSide);
    const auto estimated=n*bytesPerPixel+tiles*sizeof(Raster::Tile)*3;
    if(estimated>l.maxWorkingBytes)throw std::runtime_error("Filter exceeds transient allocation budget");return std::size_t(n);
}
void checkSelection(const GrayRaster& m){if(m.width<1||m.height<1||m.width>30000||m.height>30000||std::uint64_t(m.width)*m.height>limits::surfacePixels||m.pixels.size()!=std::size_t(m.width)*m.height)throw std::runtime_error("Invalid source selection storage");}
bool empty(const GrayRaster& m){return std::none_of(m.pixels.begin(),m.pixels.end(),[](auto p){return p!=0;});}
double normalized(double value,double low,double high,double fallback){return std::isfinite(value)?std::clamp(value,low,high):fallback;}
void kindCheck(Kind k){switch(k){case Kind::GaussianBlur:case Kind::MotionBlur:case Kind::AddNoise:case Kind::LensCorrection:case Kind::ContentAwareFill:case Kind::Vignette:case Kind::BloomGlow:case Kind::TonalContrast:return;}throw std::runtime_error("Unsupported filter kind");}
graphics::Rgba8View view(std::vector<std::uint8_t>& v,int w,int h){return {v,std::uint32_t(w),std::uint32_t(h),std::size_t(w)*4};}
std::uint8_t byte(double x){return std::uint8_t(std::clamp(std::floor(x+.5),0.,255.));}
std::vector<double> gaussianWeights(double sigma){int radius=std::max(1,int(std::ceil(sigma*3)));std::vector<double> weights(std::size_t(radius)*2+1);double total=0;for(int t=-radius;t<=radius;++t)total+=(weights[t+radius]=std::exp(-double(t*t)/(2*sigma*sigma)));for(auto& weight:weights)weight/=total;return weights;}
std::vector<std::uint8_t> gaussian(const std::vector<std::uint8_t>& source,int w,int h,double sigma,const Limits& limits){
    auto weights=gaussianWeights(sigma);int r=int(weights.size()/2);std::vector<float> pass(std::size_t(w)*h);std::vector<std::uint8_t> output(source.size());
    // Transparent exterior. Float intermediate prevents repeated 8-bit quantization.
    for(int c=0;c<4;++c){for(int y=0;y<h;++y){cancel(limits);for(int x=0;x<w;++x){if((x&255)==0)cancel(limits);double sum=0;for(int k=std::max(-r,-x);k<=std::min(r,w-1-x);++k)sum+=source[(std::size_t(y)*w+x+k)*4+c]*weights[k+r];pass[std::size_t(y)*w+x]=float(sum);}}
        for(int y=0;y<h;++y){cancel(limits);for(int x=0;x<w;++x){if((x&255)==0)cancel(limits);double sum=0;for(int k=std::max(-r,-y);k<=std::min(r,h-1-y);++k)sum+=pass[std::size_t(y+k)*w+x]*weights[k+r];output[(std::size_t(y)*w+x)*4+c]=byte(sum);}}}
    return output;
}
double sample(const std::vector<std::uint8_t>& p,int w,int h,double x,double y,int channel){
    if(x<=-1||y<=-1||x>=w||y>=h)return 0;int ix=int(std::floor(x)),iy=int(std::floor(y));double fx=x-ix,fy=y-iy;auto get=[&](int xx,int yy){return xx<0||yy<0||xx>=w||yy>=h?0.:double(p[(std::size_t(yy)*w+xx)*4+channel]);};return (get(ix,iy)*(1-fx)+get(ix+1,iy)*fx)*(1-fy)+(get(ix,iy+1)*(1-fx)+get(ix+1,iy+1)*fx)*fy;
}
std::vector<std::uint8_t> motion(const std::vector<std::uint8_t>& source,int w,int h,double sigma,double angle,const Limits& limits){
    auto weights=gaussianWeights(sigma);int r=int(weights.size()/2);const double a=angle*std::numbers::pi/180,dx=std::cos(a),dy=-std::sin(a);std::vector<std::uint8_t> out(source.size());
    for(int y=0;y<h;++y){cancel(limits);for(int x=0;x<w;++x){if((x&63)==0)cancel(limits);std::array<double,4> sum{};for(int k=-r;k<=r;++k)for(int c=0;c<4;++c)sum[c]+=sample(source,w,h,x+k*dx,y+k*dy,c)*weights[k+r];for(int c=0;c<4;++c)out[(std::size_t(y)*w+x)*4+c]=byte(sum[c]);}}return out;
}
std::shared_ptr<const Raster> resize(const Raster& source,int w,int h,const Limits& limits){
    count(w,h,limits);std::vector<std::uint8_t> out(std::size_t(w)*h*4);
    // Filters.prepared uses BrushRaster.draw with interpolationQuality=.none.
    // Read source tiles at destination pixel centers, preserving premultiplication
    // and avoiding a second full source RGBA allocation for the preview copy.
    for(int y=0;y<h;++y){cancel(limits);const int sy=int((std::int64_t(2*y+1)*source.height)/(2*h));
        for(int x=0;x<w;++x){const int sx=int((std::int64_t(2*x+1)*source.width)/(2*w));
            const auto p=source.pixel(sx,sy);const auto i=(std::size_t(y)*w+x)*4;
            out[i]=p.r;out[i+1]=p.g;out[i+2]=p.b;out[i+3]=p.a;}}
    return Raster::fromRgba(w,h,out.data(),std::size_t(w)*4);
}
PixelRect coverageBounds(const SourceSelection& selected){const auto&m=*selected.coverage;int loX=m.width,loY=m.height,hiX=0,hiY=0;for(int y=0;y<m.height;++y)for(int x=0;x<m.width;++x)if(m.pixel(x,y)){loX=std::min(loX,x);loY=std::min(loY,y);hiX=std::max(hiX,x+1);hiY=std::max(hiY,y+1);}return {selected.originX+loX,selected.originY+loY,hiX-loX,hiY-loY};}
}
Settings Settings::normalized()const{auto out=*this;out.radius=filters::normalized(radius,.1,250,1);out.angle=filters::normalized(angle,-90,90,0);out.distance=filters::normalized(distance,1,2000,10);out.amount=filters::normalized(amount,.1,400,10);out.distortion=filters::normalized(distortion,-100,100,0);
 out.vignetteAmount=filters::normalized(vignetteAmount,0,100,35);out.vignetteRed=filters::normalized(vignetteRed,0,1,0);out.vignetteGreen=filters::normalized(vignetteGreen,0,1,0);out.vignetteBlue=filters::normalized(vignetteBlue,0,1,0);out.vignetteMidpoint=filters::normalized(vignetteMidpoint,0,100,50);out.vignetteRoundness=filters::normalized(vignetteRoundness,-100,100,100);out.vignetteFeather=filters::normalized(vignetteFeather,0,100,60);out.vignetteHighlights=filters::normalized(vignetteHighlights,0,100,25);
 out.bloomAmount=filters::normalized(bloomAmount,0,100,40);out.bloomRadius=filters::normalized(bloomRadius,1,150,24);out.tonalAmount=filters::normalized(tonalAmount,0,100,50);out.tonalRadius=filters::normalized(tonalRadius,1,100,16);out.tonalShadows=filters::normalized(tonalShadows,-100,100,40);out.tonalMidtones=filters::normalized(tonalMidtones,-100,100,60);out.tonalHighlights=filters::normalized(tonalHighlights,-100,100,30);return out;}
double blurMargin(Kind kind,const Settings& raw){kindCheck(kind);auto s=raw.normalized();return kind==Kind::GaussianBlur?s.radius*3+2:kind==Kind::MotionBlur?s.distance/2+2:0;}
Transform placedGrid(const Transform& placed,int originalWidth,int originalHeight,PixelRect rect){
    if(!placed.valid()||originalWidth<1||originalHeight<1||rect.width<1||rect.height<1)throw std::runtime_error("Invalid filter placement");auto out=placed;out.width=rect.width*placed.width/originalWidth;out.height=rect.height*placed.height/originalHeight;auto middle=placed.fromUnit({(rect.x+rect.width/2.)/originalWidth,(rect.y+rect.height/2.)/originalHeight});out.x=middle.x-out.width/2;out.y=middle.y-out.height/2;if(!out.valid())throw std::runtime_error("Expanded filter transform exceeds document limits");return out;
}
std::shared_ptr<const Raster> runPixels(Kind kind,const Raster& source,const Settings& raw,double scale,std::uint32_t seed,const GrayRaster* selection,const Limits& limits,bool vignetteFillsClear){
    kindCheck(kind);cancel(limits);count(source.width,source.height,limits,kind==Kind::ContentAwareFill?32:24);if(!std::isfinite(scale)||scale<=0||scale>32)throw std::runtime_error("Invalid filter preview scale");if(selection){checkSelection(*selection);if(selection->width!=source.width||selection->height!=source.height)throw std::runtime_error("Filter selection dimensions differ");}
    if(kind==Kind::ContentAwareFill&&!selection)throw std::runtime_error("Content-Aware Fill needs a selection");const auto s=raw.normalized();auto original=source.rgba();cancel(limits);auto result=original;cancel(limits);const int w=source.width,h=source.height;
    if(selection&&empty(*selection))return Raster::fromRgba(w,h,original.data(),std::size_t(w)*4);
    switch(kind){
        case Kind::GaussianBlur:result=gaussian(original,w,h,s.radius*scale,limits);break;
        case Kind::MotionBlur:result=motion(original,w,h,s.distance*scale/std::sqrt(12.),s.angle,limits);break;
        case Kind::AddNoise:graphics::addNoise(view(result,w,h),float(s.amount),s.gaussian,s.monochromatic,seed,limits.cancelled);break;
        case Kind::Vignette:adjust_colored_vignette(result.data(),w,h,size_t(w)*4,0,0,w,h,vignetteFillsClear?1:0,s.vignetteAmount,s.vignetteMidpoint,s.vignetteRoundness,s.vignetteFeather,s.vignetteHighlights,s.vignetteRed,s.vignetteGreen,s.vignetteBlue);break;
        case Kind::BloomGlow:{if(s.bloomAmount==0)break;auto softened=gaussian(original,w,h,s.bloomRadius*scale,limits);for(size_t i=0;i<result.size();i+=4){double a=original[i+3];if(a==0)continue;double ba=softened[i+3];for(size_t c=0;c<3;++c){double base=original[i+c]/a,glow=ba>0?softened[i+c]/ba:0;result[i+c]=byte(std::clamp(base+(1-base)*glow*s.bloomAmount/50,0.,1.)*a);}}break;}
        case Kind::TonalContrast:{if(s.tonalAmount==0)break;auto softened=gaussian(original,w,h,s.tonalRadius*scale,limits);adjust_tonal_contrast(result.data(),softened.data(),w,h,size_t(w)*4,size_t(w)*4,s.tonalAmount,s.tonalShadows,s.tonalMidtones,s.tonalHighlights);break;}
        case Kind::LensCorrection:graphics::lensDistort(graphics::readOnly(view(original,w,h)),view(result,w,h),s.distortion/100*.35,limits.cancelled);break;
        case Kind::ContentAwareFill:{graphics::ConstGray8View mask{selection->pixels,std::uint32_t(w),std::uint32_t(h),std::size_t(w)};if(!graphics::contentFill(view(result,w,h),mask,limits.cancelled))throw std::runtime_error("Not enough unselected opaque pixels to synthesize a fill");break;}
    }
    cancel(limits);if(selection)for(int y=0;y<h;++y){cancel(limits);for(int x=0;x<w;++x){auto i=std::size_t(y)*w+x;unsigned coverage=selection->pixels[i];for(int c=0;c<4;++c)result[i*4+c]=std::uint8_t((unsigned(result[i*4+c])*coverage+unsigned(original[i*4+c])*(255-coverage)+127)/255);}}
    graphics::clampPremultiplied(view(result,w,h));return Raster::fromRgba(w,h,result.data(),std::size_t(w)*4);
}
Result apply(const Request& request){
    kindCheck(request.kind);cancel(request.limits);if(!request.source||!request.transform.valid())throw std::runtime_error("Filter needs a valid image layer");const auto& original=*request.source;count(original.width,original.height,request.limits);if(!std::isfinite(request.retainedBlurMargin)||request.retainedBlurMargin<0||request.retainedBlurMargin>1002)throw std::runtime_error("Invalid retained blur margin");
    Result result{request.source,request.transform,{0,0,original.width,original.height},1,false,{}};auto settings=request.settings.normalized();if(request.selection){if(!request.selection->coverage)throw std::runtime_error("Present selection must have coverage");checkSelection(*request.selection->coverage);if(std::abs(std::int64_t(request.selection->originX))>30000||std::abs(std::int64_t(request.selection->originY))>30000)throw std::runtime_error("Source selection origin exceeds supported extent");if(empty(*request.selection->coverage))return result;}
    if(request.kind==Kind::ContentAwareFill&&!request.selection)throw std::runtime_error("Content-Aware Fill needs a selection");if(request.kind==Kind::LensCorrection&&settings.distortion==0)return result;
    PixelRect bounds{0,0,original.width,original.height};const bool spreads=request.kind==Kind::GaussianBlur||request.kind==Kind::MotionBlur;
    if(spreads){int margin=int(std::ceil(std::max(request.retainedBlurMargin,blurMargin(request.kind,settings))));bounds={-margin,-margin,original.width+2*margin,original.height+2*margin};}
    if(request.kind==Kind::ContentAwareFill){auto selection=coverageBounds(*request.selection);int loX=std::min(0,selection.x),loY=std::min(0,selection.y);bounds={loX,loY,std::max(original.width,selection.x+selection.width)-loX,std::max(original.height,selection.y+selection.height)-loY};}
    count(bounds.width,bounds.height,request.limits,request.kind==Kind::ContentAwareFill?32:24);auto working=request.source;
    if(bounds!=PixelRect{0,0,original.width,original.height}){std::vector<std::uint8_t> pixels(std::size_t(bounds.width)*bounds.height*4);for(int y=0;y<original.height;++y){cancel(request.limits);for(int x=0;x<original.width;++x){auto p=original.pixel(x,y);auto i=(std::size_t(y-bounds.y)*bounds.width+x-bounds.x)*4;pixels[i]=p.r;pixels[i+1]=p.g;pixels[i+2]=p.b;pixels[i+3]=p.a;}}working=Raster::fromRgba(bounds.width,bounds.height,pixels.data(),std::size_t(bounds.width)*4);}
    auto placed=placedGrid(request.transform,original.width,original.height,bounds);double factor=request.preview&&request.kind!=Kind::AddNoise&&request.kind!=Kind::ContentAwareFill?std::min(1.,2048./std::max(working->width,working->height)):1.;
    if(factor<1){int w=std::max(1,int(working->width*factor)),h=std::max(1,int(working->height*factor));factor=double(w)/working->width;working=resize(*working,w,h,request.limits);}
    std::optional<GrayRaster> selected;if(request.selection){const auto& input=*request.selection;selected=GrayRaster{working->width,working->height,std::vector<std::uint8_t>(std::size_t(working->width)*working->height)};for(int y=0;y<working->height;++y)for(int x=0;x<working->width;++x){int sx=int(std::floor(bounds.x+(x+.5)*bounds.width/working->width))-input.originX,sy=int(std::floor(bounds.y+(y+.5)*bounds.height/working->height))-input.originY;selected->pixels[std::size_t(y)*working->width+x]=input.coverage->pixel(sx,sy);}}
    auto output=runPixels(request.kind,*working,settings,factor,request.seed,selected?&*selected:nullptr,request.limits,request.vignetteFillsClear);
    if(spreads&&!request.preview){int loX=output->width,loY=output->height,hiX=0,hiY=0;for(int y=0;y<output->height;++y){cancel(request.limits);for(int x=0;x<output->width;++x)if(output->pixel(x,y).a){loX=std::min(loX,x);loY=std::min(loY,y);hiX=std::max(hiX,x+1);hiY=std::max(hiY,y+1);}}
        if(hiX>loX&&hiY>loY&&(loX||loY||hiX!=output->width||hiY!=output->height)){const int w=hiX-loX,h=hiY-loY;std::vector<std::uint8_t> cropped(std::size_t(w)*h*4);for(int y=0;y<h;++y)for(int x=0;x<w;++x){auto p=output->pixel(x+loX,y+loY);auto i=(std::size_t(y)*w+x)*4;cropped[i]=p.r;cropped[i+1]=p.g;cropped[i+2]=p.b;cropped[i+3]=p.a;}bounds={bounds.x+loX,bounds.y+loY,w,h};placed=placedGrid(request.transform,original.width,original.height,bounds);output=Raster::fromRgba(w,h,cropped.data(),std::size_t(w)*4);}
    }
    cancel(request.limits);result.raster=output;result.transform=placed;result.sourceBounds=bounds;result.previewScale=factor;result.changed=true;result.implementation=spreads?"analytic software kernel; Mac raster comparison pending":"pinned upstream C algorithm";return result;
}
}

