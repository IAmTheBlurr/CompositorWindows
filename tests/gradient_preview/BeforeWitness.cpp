#include "editing/PixelEdits.h"
#include "core/DocumentLimits.h"
#ifndef FROZEN_BASELINE
#include "editing/GradientPreview.h"
#endif
#include <iostream>
#include <set>
#include <string>
using namespace compositor;
using namespace compositor::editing;
Document fixture(int w,int h){Document d;d.id="gradient-document";d.width=w;d.height=h;Layer l;l.id="gradient-target";l.transform={0,0,double(w),double(h)};d.layers={l};return d;}
GradientSettings settings(){GradientSettings s;s.style=GradientStyle::ForegroundToBackground;return s;}
uint64_t preview(const Document& d){
#ifdef FROZEN_BASELINE
    auto layer=gradientLayer(d.layers.front(),d,{.5,.5},{double(d.width)-.5,.5},settings());
    std::set<const Raster::Tile*> unique;if(layer.raster)for(const auto& tile:layer.raster->tiles)unique.insert(tile.get());return unique.size()*sizeof(Raster::Tile);
#else
    GradientPreview p(d.layers.front(),d,{.5,.5},{double(d.width)-.5,.5},settings());return p.previewOwnedPixelBytes();
#endif
}
int main(){int failed=0;auto run=[&](const char* name,auto body){try{const auto ok=body();std::cout<<(ok?"PASS ":"FAIL ")<<name<<'\n';failed+=!ok;}catch(const std::exception& e){std::cout<<"FAIL "<<name<<" unexpected="<<e.what()<<'\n';++failed;}};
// Upstream 1.2.11 separates per-surface and aggregate budgets. Fill the aggregate
// with shared solid tiles and immutable dense masks, leaving less than this edit needs.
auto fillBudget=[](Document&d,bool mask){uint64_t remaining=limits::documentPixels()-1000;int index=0;auto sharedMask=std::make_shared<GrayRaster>(GrayRaster{1000,1000,std::vector<uint8_t>(1000000,255)});while(remaining){const int side=mask?1000:10000;int width=remaining>=uint64_t(side)?side:int(remaining);int height=int(std::min<uint64_t>(side,remaining/width));Layer other;other.id="budget-"+std::to_string(index++);if(mask)other.mask=Mask{width==1000&&height==1000?sharedMask:std::make_shared<GrayRaster>(GrayRaster{width,height,std::vector<uint8_t>(size_t(width)*height,255)})};else other.raster=Raster::filled(width,height);d.layers.push_back(other);remaining-=uint64_t(width)*height;}};
run("aggregate_image_budget_rejected_before_preview",[&]{auto d=fixture(128,128);d.layers[0].transform={0,0,4,4};d.layers[0].raster=Raster::filled(4,4);fillBudget(d,false);validateDocument(d);try{(void)preview(d);return false;}catch(const std::exception&){return true;}});
run("owned_mask_budget_couples_image_growth",[&]{auto d=fixture(128,128);d.layers[0].transform={0,0,4,4};d.layers[0].raster=Raster::filled(4,4);d.layers[0].mask=Mask{std::make_shared<GrayRaster>(GrayRaster{1,1,{255}})};fillBudget(d,true);validateDocument(d);try{(void)preview(d);return false;}catch(const std::exception&){return true;}});
run("preview_pixels_bounded_before_viewport_request",[]{auto d=fixture(2048,2048);const auto bytes=preview(d);std::cout<<"owned_preview_pixel_bytes="<<bytes<<" maximum="<<16*sizeof(Raster::Tile)<<'\n';return bytes<=16*sizeof(Raster::Tile);});
run("unselected_blank_30000_square_still_rejected",[]{auto d=fixture(30000,30000);try{(void)preview(d);return false;}catch(const std::exception&){return true;}});
std::cout<<"cases=4 failed="<<failed<<" mac_differential=false\n";return failed?1:0;}
