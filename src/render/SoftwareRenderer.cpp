#include "core/DocumentLimits.h"
#include "core/Document.h"
#include "graphics/StackRenderer.h"
#include "effects/Adjustments.h"
#include "effects/LayerEffects.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace compositor {
namespace {
struct Prepared {Document document;std::shared_ptr<const LayerRenderPreview> preview;};
Prepared prepare(const Document& document,std::shared_ptr<const LayerRenderPreview> preview){Prepared result{document,preview};for(auto& layer:result.document.layers){if(preview&&preview->layer.id==layer.id)layer=preview->layer;if(layer.effectsJson.empty())continue;auto rendered=effects::renderedLayerEffects(layer,preview&&preview->layer.id==layer.id?preview.get():nullptr);if(rendered.raster!=layer.raster&&preview&&preview->layer.id==layer.id)result.preview.reset();layer=std::move(rendered);}return result;}
graphics::AdjustmentCallback callback(){return [](const Layer& layer,std::shared_ptr<const Raster> current,graphics::RenderRegion region){return effects::applyAdjustment(std::move(current),layer.adjustmentJson,nullptr,{region.x,region.y,region.unitsPerPixel});};}
std::shared_ptr<const Raster> render(const Document& document,std::shared_ptr<const LayerRenderPreview> preview,double x,double y,int width,int height,double units,bool cursor){
    if(width<1||height<1||width>(cursor?1024:30000)||height>(cursor?1024:30000)||uint64_t(width)*height>limits::surfacePixels||!std::isfinite(units)||units<=0||units>32768||(!cursor&&units<1./32)||!std::isfinite(x)||!std::isfinite(y)||std::abs(x)>10000000||std::abs(y)>10000000||!std::isfinite(x+width*units)||!std::isfinite(y+height*units))throw std::invalid_argument("Invalid render region");
    auto prepared=prepare(document,std::move(preview));double margin=0;for(const auto& layer:prepared.document.layers)if(layer.visible&&!layer.adjustmentJson.empty())margin+=effects::adjustmentSamplingMargin(layer.adjustmentJson);
    int halo=int(std::ceil(margin/units));if(halo>15000||width+2LL*halo>30000||height+2LL*halo>30000||uint64_t(width+2LL*halo)*(height+2LL*halo)>limits::surfacePixels)throw std::runtime_error("Adjustment sampling exceeds the working surface budget");
    graphics::StackRenderer backend(callback(),prepared.preview);
    auto image=cursor&&halo==0?backend.renderCursorRegion(prepared.document,x,y,width,height,units):backend.renderScaled(prepared.document,x-halo*units,y-halo*units,width+2*halo,height+2*halo,units);
    if(!halo)return image;auto result=Raster::filled(width,height);for(int ty=0;ty<height;ty+=256)for(int tx=0;tx<width;tx+=256){int w=std::min(256,width-tx),h=std::min(256,height-ty);std::vector<Pixel> pixels(size_t(w)*h);for(int py=0;py<h;++py)for(int px=0;px<w;++px)pixels[size_t(py)*w+px]=image->pixel(tx+px+halo,ty+py+halo);result=result->replacing(tx,ty,w,h,pixels.data(),w);}return result;
}
}
std::shared_ptr<const Raster> SoftwareRenderer::renderScaledPatch(const Document& document,double x,double y,int ox,int oy,int width,int height,double units)const {auto prepared=prepare(document,preview_);return graphics::StackRenderer({},prepared.preview).renderScaledPatch(prepared.document,x,y,ox,oy,width,height,units);}
std::shared_ptr<const Raster> SoftwareRenderer::render(const Document& document,int x,int y,int width,int height)const {return compositor::render(document,preview_,x,y,width,height,1,false);}
std::shared_ptr<const Raster> SoftwareRenderer::renderScaled(const Document& document,double x,double y,int width,int height,double units)const {return compositor::render(document,preview_,x,y,width,height,units,false);}
std::shared_ptr<const Raster> SoftwareRenderer::renderCursorRegion(const Document& document,double x,double y,int width,int height,double units)const {return compositor::render(document,preview_,x,y,width,height,units,true);}
}
