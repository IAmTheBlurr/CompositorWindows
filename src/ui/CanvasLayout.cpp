#include "NativeCanvas.h"
#include <QImage>
#include <QPainter>
#include <algorithm>
#include <cmath>

namespace compositor {
bool NativeCanvas::beginGuide(QPointF point){
    if(documentWidth_<=0||lockGuides||(canEditGuide&&!canEditGuide()))return false;
    const auto at=documentPoint(point);
    if(showRulers&&(point.y()<22||point.x()<22)){
        const auto axis=point.y()<22?CanvasGuide::Axis::Horizontal:CanvasGuide::Axis::Vertical;
        draggedGuide_=CanvasGuide{newId(),axis,axis==CanvasGuide::Axis::Vertical?at.x():at.y()};newGuide_=true;showGuides=true;return true;
    }
    if(!showGuides)return false;
    double distance=5;
    for(const auto&guide:guides){auto view=viewMapping().toView({guide.position,guide.position});double d=std::abs(guide.axis==CanvasGuide::Axis::Vertical?view.x-point.x():view.y-point.y());if(d<=distance){distance=d;draggedGuide_=guide;}}
    newGuide_=false;return draggedGuide_.has_value();
}
void NativeCanvas::updateGuide(QPointF point,bool finish){
    if(!draggedGuide_)return;
    const auto at=documentPoint(point);auto&guide=*draggedGuide_;
    guide.position=std::clamp(guide.axis==CanvasGuide::Axis::Vertical?at.x():at.y(),-1000000.,1000000.);
    if(guideSnap)guide.position=guideSnap(guide.position,guide.axis,guide.id);
    if(finish){auto committed=guide;const bool remove=(showRulers&&(point.x()<22||point.y()<22))||!rect().contains(point.toPoint());draggedGuide_.reset();if(guideCommitted&&!(remove&&newGuide_))guideCommitted(committed,remove);}
    update();
}
void NativeCanvas::drawLayout(){
    if(!context_||documentWidth_<=0||documentHeight_<=0)return;
    const auto mapping=viewMapping();auto point=[&](double x,double y){auto p=mapping.toView({x,y});return D2D1::Point2F(float(p.x),float(p.y));};
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush;
    if(FAILED(context_->CreateSolidColorBrush(D2D1::ColorF(0,1,1,.85f),&brush)))return;
    if(showLayoutGrid){
        const double step=pointsPerPixel()*8>=4?8:64;
        const auto start=documentPoint({0,0}),end=documentPoint({double(width()),double(height())});
        const auto origin=point(0,0),last=point(documentWidth_,documentHeight_);
        context_->PushAxisAlignedClip(D2D1::RectF(origin.x,origin.y,last.x,last.y),D2D1_ANTIALIAS_MODE_ALIASED);
        for(double x=std::max(0.,std::ceil(start.x()/step)*step);x<=std::min(double(documentWidth_),end.x());x+=step){brush->SetColor(D2D1::ColorF(.6f,.6f,.6f,std::fmod(x,64)==0?.55f:.25f));context_->DrawLine(point(x,0),point(x,documentHeight_),brush.Get(),float(1/backingScale_));}
        for(double y=std::max(0.,std::ceil(start.y()/step)*step);y<=std::min(double(documentHeight_),end.y());y+=step){brush->SetColor(D2D1::ColorF(.6f,.6f,.6f,std::fmod(y,64)==0?.55f:.25f));context_->DrawLine(point(0,y),point(documentWidth_,y),brush.Get(),float(1/backingScale_));}
        context_->PopAxisAlignedClip();
    }
    if(showGuides){
        brush->SetColor(D2D1::ColorF(0,1,1,.85f));auto line=[&](const CanvasGuide&g){if(g.axis==CanvasGuide::Axis::Vertical)context_->DrawLine(point(g.position,0),point(g.position,documentHeight_),brush.Get(),1);else context_->DrawLine(point(0,g.position),point(documentWidth_,g.position),brush.Get(),1);};
        for(const auto&g:guides)if(!draggedGuide_||g.id!=draggedGuide_->id)line(g);
        if(draggedGuide_)line(*draggedGuide_);
    }
    if(!showRulers)return;
    // Text is drawn by Qt into two small strips; the scene remains a native D2D surface.
    for(bool vertical:{false,true}){
        QSize logical=vertical?QSize(22,height()):QSize(width(),22);
        QImage image(QSize(int(std::ceil(logical.width()*backingScale_)),int(std::ceil(logical.height()*backingScale_))),QImage::Format_ARGB32_Premultiplied);image.setDevicePixelRatio(backingScale_);image.fill(QColor(37,38,42));
        QPainter painter(&image);painter.setPen(QColor(175,179,188));auto font=painter.font();font.setPixelSize(9);painter.setFont(font);
        double interval=1;while(interval*pointsPerPixel()<60)interval*=2;
        const double origin=vertical?mapping.origin.y:mapping.origin.x;
        const double length=vertical?height():width();
        const auto first=std::floor(-origin/pointsPerPixel()/interval)*interval;
        for(double value=first;origin+value*pointsPerPixel()<length;value+=interval){const double pos=origin+value*pointsPerPixel();if(vertical){painter.drawLine(QPointF(15,pos),QPointF(22,pos));painter.save();painter.translate(3,pos+3);painter.rotate(90);painter.drawText(0,0,QString::number(value));painter.restore();}else{painter.drawLine(QPointF(pos,15),QPointF(pos,22));painter.drawText(QPointF(pos+3,11),QString::number(value));}}
        painter.end();Microsoft::WRL::ComPtr<ID2D1Bitmap1> bitmap;
        const auto properties=D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_NONE,D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED),float(96*backingScale_),float(96*backingScale_));
        if(SUCCEEDED(context_->CreateBitmap(D2D1::SizeU(image.width(),image.height()),image.constBits(),UINT32(image.bytesPerLine()),properties,&bitmap)))context_->DrawBitmap(bitmap.Get(),D2D1::RectF(0,0,float(logical.width()),float(logical.height())),1,D2D1_INTERPOLATION_MODE_LINEAR);
    }
}
}
