// ShapeTool.swift at a19db9011282399785dc18efcfded904627bdcc2.
// Copyright (c) 2026 Wonder Assembly LLC; MIT notice: graphics/upstream/LICENSE.
#include "Shapes.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonParseError>
#include <QImage>
#include <QPainter>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <numbers>

namespace compositor::editing {
namespace {
void validate(const ShapeStyle& s){if(s.kind!=ShapeKind::Rectangle&&s.kind!=ShapeKind::Ellipse&&s.kind!=ShapeKind::Line)throw std::runtime_error("Invalid shape kind");for(double c:{s.red,s.green,s.blue})if(!std::isfinite(c))throw std::runtime_error("Invalid shape color");if(!std::isfinite(s.cornerRadius))throw std::runtime_error("Invalid shape radius");if(s.lineWidth&&(!std::isfinite(*s.lineWidth)||*s.lineWidth<1||*s.lineWidth>5000))throw std::runtime_error("Invalid line width");for(const auto& point:{s.start,s.end})if(point&&(!std::isfinite(point->x)||!std::isfinite(point->y)||point->x<0||point->x>1||point->y<0||point->y>1))throw std::runtime_error("Invalid line endpoint");}
uint8_t byte(double value){return uint8_t(std::clamp(std::lround(value),0L,255L));}
const char* kindName(ShapeKind kind){return kind==ShapeKind::Rectangle?"Rectangle":kind==ShapeKind::Ellipse?"Ellipse":"Line";}
Layer drawLayer(const Layer& source,const ShapeStyle& style,bool force){
    if(!source.raster||source.shapeJson.empty())throw std::runtime_error("Layer is no longer a live shape");if(!source.transform.valid())throw std::runtime_error("Invalid shape transform");
    int width=std::max(1,int(std::round(source.transform.width))),height=std::max(1,int(std::round(source.transform.height)));
    if(!force&&width==source.raster->width&&height==source.raster->height)return source;
    auto result=source;result.raster=shapeRaster(style,width,height);result.shapeJson=encodeShapeStyle(style);if(result.mask&&!result.mask->placement)result.mask->placement=source.transform;return result;
}
}
ShapeStyle decodeShapeStyle(std::string_view json){
    if(json.size()>65536)throw std::runtime_error("Shape style JSON exceeds budget");QJsonParseError error;auto doc=QJsonDocument::fromJson(QByteArray(json.data(),qsizetype(json.size())),&error);if(error.error!=QJsonParseError::NoError||!doc.isObject())throw std::runtime_error("Invalid shape style JSON");auto object=doc.object();auto kind=object.value("kind");if(!kind.isString()||(kind.toString()!="Rectangle"&&kind.toString()!="Ellipse"&&kind.toString()!="Line"))throw std::runtime_error("Invalid shape kind");
    auto number=[&](const char* name){auto value=object.value(QLatin1String(name));if(!value.isDouble()||!std::isfinite(value.toDouble()))throw std::runtime_error(std::string("Missing or invalid shape field: ")+name);return value.toDouble();};
    ShapeStyle style{kind.toString()=="Rectangle"?ShapeKind::Rectangle:kind.toString()=="Ellipse"?ShapeKind::Ellipse:ShapeKind::Line,number("red"),number("green"),number("blue"),number("cornerRadius")};
    if(!object.value("lineWidth").isUndefined()&&!object.value("lineWidth").isNull())style.lineWidth=number("lineWidth");
    auto point=[&](const char* field)->std::optional<Point>{auto value=object.value(QLatin1String(field));if(value.isUndefined()||value.isNull())return {};auto pair=value.toArray();if(!value.isArray()||pair.size()!=2||!pair[0].isDouble()||!pair[1].isDouble())throw std::runtime_error("Invalid line endpoint");return Point{pair[0].toDouble(),pair[1].toDouble()};};style.start=point("start");style.end=point("end");validate(style);return style;
}
std::string encodeShapeStyle(const ShapeStyle& style){validate(style);QJsonObject object{{"kind",QLatin1String(kindName(style.kind))},{"red",style.red},{"green",style.green},{"blue",style.blue},{"cornerRadius",style.cornerRadius}};if(style.lineWidth)object["lineWidth"]=*style.lineWidth;if(style.start)object["start"]=QJsonArray{style.start->x,style.start->y};if(style.end)object["end"]=QJsonArray{style.end->x,style.end->y};return QJsonDocument(object).toJson(QJsonDocument::Compact).toStdString();}
std::shared_ptr<const Raster> shapeRaster(const ShapeStyle& style,int width,int height){
    validate(style);
    if(style.kind==ShapeKind::Line){
        if(width<1||height<1||width>30000||height>30000||uint64_t(width)*height>200000000)throw std::runtime_error("Line exceeds surface budget");
        QImage image(width,height,QImage::Format_RGBA8888_Premultiplied);if(image.isNull())throw std::runtime_error("Not enough memory to draw line");image.fill(Qt::transparent);const double thickness=style.lineWidth.value_or(1);
        const auto from=style.start.value_or(Point{std::min(thickness,double(width))/(2*width),std::min(thickness,double(height))/(2*height)}),to=style.end.value_or(Point{1-std::min(thickness,double(width))/(2*width),1-std::min(thickness,double(height))/(2*height)});
        {QPainter painter(&image);painter.setRenderHint(QPainter::Antialiasing);painter.setPen(QPen(QColor::fromRgbF(std::clamp(style.red,0.,1.),std::clamp(style.green,0.,1.),std::clamp(style.blue,0.,1.)),thickness,Qt::SolidLine,Qt::RoundCap));painter.drawLine(QPointF(from.x*width,from.y*height),QPointF(to.x*width,to.y*height));}
        return Raster::fromRgba(width,height,image.constBits(),size_t(image.bytesPerLine()));
    }
    auto out=std::make_shared<Raster>(*Raster::filled(width,height));Rect rect{0,0,double(width),double(height)};auto outline=style.kind==ShapeKind::Ellipse?SelectionOutline::ellipse(rect):SelectionOutline::roundedRectangle(rect,style.cornerRadius);auto coverage=outline.rasterize(width,height);int columns=(width+255)/256;
    for(size_t key=0;key<out->tiles.size();++key){auto tile=std::make_shared<Raster::Tile>();int left=int(key%columns)*256,top=int(key/columns)*256;for(int y=0;y<std::min(256,height-top);++y)for(int x=0;x<std::min(256,width-left);++x){auto alpha=coverage->pixel(left+x,top+y);tile->pixels[size_t(y)*256+x]={byte(std::clamp(style.red,0.,1.)*alpha),byte(std::clamp(style.green,0.,1.)*alpha),byte(std::clamp(style.blue,0.,1.)*alpha),alpha};}out->tiles[key]=tile;}return out;
}
Point snappedLineEnd(Point start,Point end,bool snapAngle){if(!snapAngle)return end;const double angle=std::round(std::atan2(end.y-start.y,end.x-start.x)/(std::numbers::pi/4))*(std::numbers::pi/4),length=std::hypot(end.x-start.x,end.y-start.y);return {start.x+std::cos(angle)*length,start.y+std::sin(angle)*length};}
Rect lineShapeBounds(Point start,Point end,double width){return {std::min(start.x,end.x)-width/2,std::min(start.y,end.y)-width/2,std::abs(end.x-start.x)+width,std::abs(end.y-start.y)+width};}
std::optional<Layer> createLineLayer(Point start,Point end,const ShapeStyle& settings,std::string name){
    if(std::hypot(end.x-start.x,end.y-start.y)<.5)return {};auto style=settings;style.kind=ShapeKind::Line;style.cornerRadius=0;const double width=style.lineWidth.value_or(1);const auto bounds=lineShapeBounds(start,end,width);
    style.start=Point{(start.x-bounds.x)/bounds.width,(start.y-bounds.y)/bounds.height};style.end=Point{(end.x-bounds.x)/bounds.width,(end.y-bounds.y)/bounds.height};return createShapeLayer(bounds,style,std::move(name));
}
std::optional<Layer> createShapeLayer(Rect rect,const ShapeStyle& style,std::string name){
    validate(style);for(double value:{rect.x,rect.y,rect.width,rect.height})if(!std::isfinite(value))throw std::runtime_error("Invalid shape rectangle");if(rect.width<1||rect.height<1)return {};if(rect.width>30000||rect.height>30000||uint64_t(rect.width)*uint64_t(rect.height)>200000000)throw std::runtime_error("Shape exceeds pixel budget");
    Layer layer;layer.id=newId();layer.name=std::move(name);layer.transform.x=rect.x;layer.transform.y=rect.y;layer.transform.width=rect.width;layer.transform.height=rect.height;if(!layer.transform.valid())throw std::runtime_error("Invalid shape placement");layer.raster=shapeRaster(style,int(rect.width),int(rect.height));layer.shapeJson=encodeShapeStyle(style);return layer;
}
std::string nextShapeName(const Document& doc,ShapeKind kind){for(size_t number=1;;++number){std::string name=std::string(kindName(kind))+" "+std::to_string(number);if(std::none_of(doc.layers.begin(),doc.layers.end(),[&](const Layer& layer){return layer.name==name;}))return name;}}
Layer redrawShape(const Layer& layer){if(layer.shapeJson.empty()||!layer.raster)return layer;return drawLayer(layer,decodeShapeStyle(layer.shapeJson),false);}
Layer restyleShape(const Layer& layer,const ShapeStyle& style){validate(style);if(layer.shapeJson.empty()||!layer.raster)throw std::runtime_error("Layer is no longer a live shape");if(decodeShapeStyle(layer.shapeJson)==style)return layer;return drawLayer(layer,style,true);}
std::shared_ptr<const Raster> shapeTransformPreview(const Layer& layer,const Transform& transform){
    if(layer.shapeJson.empty()||!layer.raster)return {};auto style=decodeShapeStyle(layer.shapeJson);if(style.kind!=ShapeKind::Rectangle||style.cornerRadius<=0)return {};if(!transform.valid())throw std::runtime_error("Invalid shape preview transform");if(std::abs(transform.width-layer.raster->width)<.5&&std::abs(transform.height-layer.raster->height)<.5)return {};
    double factor=std::min(1.,2048/std::max(transform.width,transform.height));int width=std::max(1,int(std::round(transform.width*factor))),height=std::max(1,int(std::round(transform.height*factor)));style.cornerRadius*=factor;return shapeRaster(style,width,height);
}
}
