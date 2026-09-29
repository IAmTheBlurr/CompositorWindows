// LayerEffects.swift at Compositor v1.2.11, copyright Wonder Assembly LLC (MIT).
#include "LayerEffects.h"
#include "core/DocumentLimits.h"
#include "filters/PixelFilters.h"
#include "graphics/RasterSampling.h"
#include "graphics/SamplingSource.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>
#include <array>
#include <cmath>
#include <deque>
#include <mutex>
#include <numbers>
#include <stdexcept>

namespace compositor::effects {
namespace {
struct Effect {bool present{},enabled{true},inside{};double size{},angle{90},distance{},blur{},opacity{1},red{},green{},blue{};};
using Effects=std::array<Effect,6>;
constexpr std::array<const char*,6> keys{"stroke","shadow","colorOverlay","innerShadow","outerGlow","innerGlow"};
Effects parse(std::string_view json){
    Effects result{};if(json.empty())return result;
    if(json.size()>65536)throw std::invalid_argument("Layer effects metadata exceeds budget");
    auto document=QJsonDocument::fromJson(QByteArray(json.data(),qsizetype(json.size())));if(!document.isObject())throw std::invalid_argument("Invalid layer effects metadata");
    auto root=document.object();
    for(size_t i=0;i<keys.size();++i){auto value=root[keys[i]];if(value.isUndefined()||value.isNull())continue;if(!value.isObject())throw std::invalid_argument("Invalid layer effect");auto o=value.toObject();auto& e=result[i];e.present=true;
        auto number=[&](const char* key,double fallback,double lo,double hi){auto v=o.value(key);if(v.isUndefined())return fallback;if(!v.isDouble()||!std::isfinite(v.toDouble())||v.toDouble()<lo||v.toDouble()>hi)throw std::invalid_argument("Invalid layer effect parameter");return v.toDouble();};
        auto flag=[&](const char* key,bool fallback){auto v=o.value(key);if(v.isUndefined()||v.isNull())return fallback;if(!v.isBool())throw std::invalid_argument("Invalid layer effect flag");return v.toBool();};
        e.enabled=flag("enabled",true);e.inside=flag("inside",false);e.size=number("size",i==0?4:i==4?20:i==5?10:0,0,500);
        e.angle=number("angle",90,-360,360);e.distance=number("distance",i==1?20:i==3?10:0,0,5000);e.blur=number("blur",i==1?20:i==3?10:0,0,500);
        e.opacity=number("opacity",i==1||i==3?.5:i>=4?.75:1,0,1);e.red=number("red",i>=4?1:0,0,1);e.green=number("green",i>=4?1:0,0,1);e.blue=number("blue",i>=4?1:0,0,1);
    }return result;
}
bool visible(const Effect& e){return e.present&&e.enabled&&e.opacity>0;}
double margin(const Effects& effects){double m=0;auto s=effects[0];if(visible(s)&&!s.inside)m=s.size;s=effects[1];if(visible(s))m=std::max(m,s.distance+s.blur*3);s=effects[4];if(visible(s))m=std::max(m,s.size*3);return std::ceil(m)+2;}
uint8_t byte(double v){return uint8_t(std::clamp(std::lround(v),0L,255L));}
Pixel scaled(Pixel p,double v){return{byte(p.r*v),byte(p.g*v),byte(p.b*v),byte(p.a*v)};}
std::vector<float> extreme(const std::vector<float>& input,int w,int h,int radius,bool minimum){
    std::vector<float> pass(input.size()),result(input.size());
    auto sweep=[&](const auto& source,auto& destination,int lines,int length,int lineStep,int step){std::vector<int> queue(size_t(length),0);for(int line=0;line<lines;++line){int head=0,tail=0,next=0,base=line*lineStep;for(int center=0;center<length;++center){while(next<=std::min(length-1,center+radius)){float value=source[size_t(base+next*step)];while(tail>head){float previous=source[size_t(base+queue[size_t(tail-1)]*step)];if(minimum?previous<value:previous>value)break;--tail;}queue[size_t(tail++)]=next++;}while(head<tail&&queue[size_t(head)]<center-radius)++head;destination[size_t(base+center*step)]=minimum&&(center<radius||center+radius>=length)?0:source[size_t(base+queue[size_t(head)]*step)];}}};
    sweep(input,pass,h,w,w,1);sweep(pass,result,w,h,1,w);return result;
}
std::vector<float> blur(const std::vector<float>& input,int w,int h,double sigma){
    if(sigma<=0)return input;int r=std::max(1,int(std::lround(sigma*3)));std::vector<double> kernel(size_t(2*r+1));double total=0;for(int k=-r;k<=r;++k)total+=(kernel[size_t(k+r)]=std::exp(-double(k)*k/(2*sigma*sigma)));for(auto& k:kernel)k/=total;
    std::vector<float> pass(input.size()),out(input.size());
    for(int y=0;y<h;++y)for(int x=0;x<w;++x){double sum=0;for(int k=std::max(-r,-x);k<=std::min(r,w-1-x);++k)sum+=input[size_t(y)*w+x+k]*kernel[size_t(k+r)];pass[size_t(y)*w+x]=float(sum);}
    for(int y=0;y<h;++y)for(int x=0;x<w;++x){double sum=0;for(int k=std::max(-r,-y);k<=std::min(r,h-1-y);++k)sum+=pass[size_t(y+k)*w+x]*kernel[size_t(k+r)];out[size_t(y)*w+x]=float(sum);}return out;
}
float sample(const std::vector<float>& p,int w,int h,double x,double y){int ix=int(std::floor(x)),iy=int(std::floor(y));double fx=x-ix,fy=y-iy;auto at=[&](int xx,int yy){return xx<0||yy<0||xx>=w||yy>=h?0.f:p[size_t(yy)*w+xx];};return float((at(ix,iy)*(1-fx)+at(ix+1,iy)*fx)*(1-fy)+(at(ix,iy+1)*(1-fx)+at(ix+1,iy+1)*fx)*fy);}
Layer build(const Layer& layer,const Effects& effects,const LayerRenderPreview* preview){
    if(!layer.raster||layer.group||!layer.adjustmentJson.empty())throw std::invalid_argument("Effects require an image layer");
    int inset=int(margin(effects)),sw=preview&&preview->imageSource?preview->imageSource->width:layer.raster->width,sh=preview&&preview->imageSource?preview->imageSource->height:layer.raster->height,w=sw+2*inset,h=sh+2*inset;
    if(w>30000||h>30000||uint64_t(w)*h>limits::surfacePixels)throw std::runtime_error("Layer effects exceed the working surface budget");
    std::vector<Pixel> shown(size_t(w)*h),out(size_t(w)*h);std::vector<float> alpha(size_t(w)*h);
    for(int y=0;y<sh;++y)for(int x=0;x<sw;++x){Point u{(x+.5)/sw,(y+.5)/sh};auto p=preview&&preview->image?preview->image(u,Transform::Sampling::Nearest):layer.raster->pixel(x,y);
        if(layer.mask&&layer.mask->enabled&&layer.mask->raster){auto point=layer.transform.fromUnit(u);auto placement=layer.mask->placement.value_or(layer.transform);auto mu=placement.toUnit(point);double m=preview&&preview->mask?preview->mask(mu,placement.sampling,0):graphics::sampleMask(*layer.mask->raster,mu,placement.sampling,layer.mask->placement?graphics::maskBackground(*layer.mask->raster):0);p=scaled(p,m);}
        size_t i=size_t(y+inset)*w+x+inset;shown[i]=p;alpha[i]=p.a/255.f;
    }
    auto fill=[&](const Effect& e,const std::vector<float>& coverage){for(size_t i=0;i<out.size();++i){double a=255*e.opacity*std::clamp(double(coverage[i]),0.,1.);out[i]=blendPixel(out[i],{byte(e.red*a),byte(e.green*a),byte(e.blue*a),byte(a)},Blend::Normal);}};
    auto shadow=[&](const Effect& e,bool inside){auto softened=blur(alpha,w,h,e.blur/2);std::vector<float> coverage(alpha.size());double a=e.angle*std::numbers::pi/180,dx=-std::cos(a)*e.distance,dy=std::sin(a)*e.distance;for(int y=0;y<h;++y)for(int x=0;x<w;++x){size_t i=size_t(y)*w+x;float v=sample(softened,w,h,x-dx,y-dy);coverage[i]=inside?alpha[i]*(1-v):v;}fill(e,coverage);};
    auto glow=[&](const Effect& e,bool inside){auto coverage=blur(alpha,w,h,e.size/2);for(size_t i=0;i<alpha.size();++i)coverage[i]=inside?alpha[i]*(1-coverage[i]):coverage[i]*(1-alpha[i]);fill(e,coverage);};
    auto stroke=[&]{auto e=effects[0];if(!visible(e)||e.size<=0)return;auto coverage=extreme(alpha,w,h,std::max(1,int(std::lround(e.size))),e.inside);for(size_t i=0;i<alpha.size();++i)coverage[i]=std::max(0.f,e.inside?alpha[i]-coverage[i]:coverage[i]-alpha[i]);fill(e,coverage);};
    if(visible(effects[1]))shadow(effects[1],false);if(visible(effects[4]))glow(effects[4],false);if(!effects[0].inside)stroke();for(size_t i=0;i<out.size();++i)out[i]=blendPixel(out[i],shown[i],Blend::Normal);
    if(visible(effects[2]))fill(effects[2],alpha);if(visible(effects[5]))glow(effects[5],true);if(visible(effects[3]))shadow(effects[3],true);if(effects[0].inside)stroke();
    auto result=layer;result.raster=Raster::fromRgba(w,h,reinterpret_cast<const uint8_t*>(out.data()),size_t(w)*4);result.transform=filters::placedGrid(layer.transform,sw,sh,{-inset,-inset,w,h});result.mask.reset();result.effectsJson.clear();return result;
}
}
void validateLayerEffectsJson(std::string_view json){(void)parse(json);}
double layerEffectsMargin(std::string_view json){return json.empty()?0:margin(parse(json));}
Layer renderedLayerEffects(const Layer& layer,const LayerRenderPreview* preview){
    auto effects=parse(layer.effectsJson);if(std::none_of(effects.begin(),effects.end(),visible))return layer;
    struct Entry {Layer source,result;};static std::mutex mutex;static std::deque<Entry> cache;
    if(!preview){std::lock_guard lock(mutex);for(const auto& entry:cache)if(entry.source==layer)return entry.result;}
    auto result=build(layer,effects,preview);if(!preview&&result.raster->retainedBytes()<64*1024*1024){std::lock_guard lock(mutex);cache.push_back({layer,result});size_t total=0;for(const auto& entry:cache)total+=entry.result.raster->retainedBytes();while(cache.size()>4||total>64*1024*1024){total-=cache.front().result.raster->retainedBytes();cache.pop_front();}}
    return result;
}
}
