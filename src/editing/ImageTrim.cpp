#include "DocumentGeometry.h"
#include <algorithm>
#include <cmath>

namespace compositor::editing {
std::optional<Rect> trimBounds(const Document&document,const TrimOptions&options,std::function<bool()> cancelled){
    validateDocument(document);if(!(options.top||options.bottom||options.left||options.right))return Rect{0,0,double(document.width),double(document.height)};
    SoftwareRenderer renderer;const bool lower=options.basedOn==TrimBasis::BottomRight;
    const auto sample=renderer.render(document,lower?document.width-1:0,lower?document.height-1:0,1,1)->pixel(0,0);
    int left=document.width,top=document.height,right=0,bottom=0;
    for(int y=0;y<document.height;y+=256)for(int x=0;x<document.width;x+=256){
        if(cancelled&&cancelled())return {};
        auto raster=renderer.render(document,x,y,std::min(256,document.width-x),std::min(256,document.height-y));
        for(int py=0;py<raster->height;++py)for(int px=0;px<raster->width;++px){auto p=raster->pixel(px,py);const bool content=options.basedOn==TrimBasis::Transparent?p.a!=0:std::max({std::abs(int(p.r)-sample.r),std::abs(int(p.g)-sample.g),std::abs(int(p.b)-sample.b),std::abs(int(p.a)-sample.a)})>options.tolerance;if(content){left=std::min(left,x+px);top=std::min(top,y+py);right=std::max(right,x+px+1);bottom=std::max(bottom,y+py+1);}}
    }
    if(right<=left||bottom<=top)return {};
    if(!options.left)left=0;if(!options.right)right=document.width;if(!options.top)top=0;if(!options.bottom)bottom=document.height;
    return Rect{double(left),double(top),double(right-left),double(bottom-top)};
}
}
