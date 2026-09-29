// TypeTool.swift at 0ecbacfff8610b566eda059fb2644fddf337fb65.
// Copyright (c) 2026 Wonder Assembly LLC; MIT notice: graphics/upstream/LICENSE.
#include "Text.h"
#include <QColor>
#include <QFontDatabase>
#include <QFontMetricsF>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QPainter>
#include <QTextLayout>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace compositor::editing {
namespace {
void require(const TextStyle& style){if(!style.valid())throw std::runtime_error("Invalid text style");}
QImage metricsDevice(){QImage image(1,1,QImage::Format_RGBA8888_Premultiplied);image.setDotsPerMeterX(3780);image.setDotsPerMeterY(3780);return image;}
QString fontKey(QString name){name.remove(' ');name.remove('-');name.remove('_');if(name.endsWith("MT"))name.chop(2);return name.toCaseFolded();}
struct Layout {
    std::vector<std::unique_ptr<QTextLayout>> paragraphs;
    QSizeF size;
};
Layout makeLayout(const TextStyle& style,QPaintDevice* device){
    require(style);Layout result;
    const auto font=textFont(style);const QFontMetricsF metrics(font,device);
    const double available=style.boxSize?std::max(1.,style.boxSize->width()-2*TextStyle::padding):100000.;
    const double leading=style.lineHeight();double y=0,measured=0;
    auto content=style.content;content.replace("\r\n","\n");content.replace('\r','\n');
    for(const auto& paragraph:content.split('\n',Qt::KeepEmptyParts)){
        auto layout=std::make_unique<QTextLayout>(paragraph,font,device);
        QTextOption option;option.setWrapMode(style.boxSize?QTextOption::WrapAtWordBoundaryOrAnywhere:QTextOption::NoWrap);
        option.setAlignment(style.alignment==TextAlignment::Center?Qt::AlignHCenter:style.alignment==TextAlignment::Right?Qt::AlignRight:Qt::AlignLeft);
        option.setFlags(QTextOption::IncludeTrailingSpaces);layout->setTextOption(option);
        layout->beginLayout();bool any=false;
        for(;;){auto line=layout->createLine();if(!line.isValid())break;any=true;line.setLineWidth(available);
            line.setPosition({TextStyle::padding,TextStyle::padding+y+leading-metrics.descent()-line.ascent()});
            measured=std::max(measured,line.naturalTextWidth());y+=leading;
            if(y>30000+style.fontSize&&style.boxSize)break;
        }
        layout->endLayout();if(!any)y+=leading;result.paragraphs.push_back(std::move(layout));
    }
    result.size=style.boxSize.value_or(QSizeF(std::max(16.,std::ceil(measured+2*TextStyle::padding+style.fontSize*.1)),std::max(16.,std::ceil(y+2*TextStyle::padding))));
    if(result.size.width()>30000||result.size.height()>30000||result.size.width()*result.size.height()>200000000)
        throw std::runtime_error("Text exceeds the 30,000-pixel or 200-megapixel surface limit");
    // Point text also honors paragraph alignment, in its measured box.
    if(!style.boxSize)for(auto& paragraph:result.paragraphs)for(int i=0;i<paragraph->lineCount();++i)paragraph->lineAt(i).setLineWidth(std::max(1.,result.size.width()-2*TextStyle::padding));
    return result;
}
}
bool TextStyle::valid() const {
    if(content.size()>100000)return false;
    for(auto value:{fontSize,red,green,blue,tracking,leading})if(!std::isfinite(value))return false;
    if(fontSize<1||fontSize>2000||tracking< -100||tracking>1000||leading<0||leading>5000)return false;
    for(auto value:{red,green,blue})if(value<0||value>1)return false;
    if(alignment!=TextAlignment::Left&&alignment!=TextAlignment::Center&&alignment!=TextAlignment::Right)return false;
    if(boxSize){const double width=boxSize->width(),height=boxSize->height();if(!std::isfinite(width)||!std::isfinite(height)||width<16||height<16||width>30000||height>30000||width*height>200000000)return false;}
    return true;
}
TextStyle decodeTextStyle(std::string_view json){
    if(json.size()>2000000)throw std::runtime_error("Text metadata exceeds budget");QJsonParseError error;
    const auto document=QJsonDocument::fromJson(QByteArray(json.data(),qsizetype(json.size())),&error);
    if(error.error!=QJsonParseError::NoError||!document.isObject())throw std::runtime_error("Invalid text metadata");
    const auto object=document.object();TextStyle result;
    auto string=[&](const char* field){const auto value=object.value(QLatin1String(field));if(!value.isString())throw std::runtime_error(std::string("Missing text field: ")+field);return value.toString();};
    auto number=[&](const char* field){const auto value=object.value(QLatin1String(field));if(!value.isDouble())throw std::runtime_error(std::string("Missing text field: ")+field);return value.toDouble();};
    result.content=string("content");result.fontName=string("fontName");result.fontSize=number("fontSize");result.red=number("red");result.green=number("green");result.blue=number("blue");result.tracking=number("tracking");result.leading=number("leading");
    const auto alignment=string("alignment");if(alignment=="Left")result.alignment=TextAlignment::Left;else if(alignment=="Center")result.alignment=TextAlignment::Center;else if(alignment=="Right")result.alignment=TextAlignment::Right;else throw std::runtime_error("Invalid text alignment");
    const auto box=object.value("boxSize");if(!box.isUndefined()&&!box.isNull()){const auto size=box.toArray();if(!box.isArray()||size.size()!=2||!size[0].isDouble()||!size[1].isDouble())throw std::runtime_error("Invalid paragraph box");result.boxSize=QSizeF(size[0].toDouble(),size[1].toDouble());}
    if(object.contains("colorRuns")||object.contains("fontRuns"))throw std::runtime_error("Attributed text requires a newer project format");
    require(result);return result;
}
std::string encodeTextStyle(const TextStyle& style){
    require(style);QJsonObject object{{"content",style.content},{"fontName",style.fontName},{"fontSize",style.fontSize},{"red",style.red},{"green",style.green},{"blue",style.blue},{"tracking",style.tracking},{"leading",style.leading},{"alignment",style.alignment==TextAlignment::Center?"Center":style.alignment==TextAlignment::Right?"Right":"Left"}};
    if(style.boxSize)object["boxSize"]=QJsonArray{style.boxSize->width(),style.boxSize->height()};return QJsonDocument(object).toJson(QJsonDocument::Compact).toStdString();
}
QFont textFont(const TextStyle& style){
    QFont font(style.fontName);const auto families=QFontDatabase::families();
    // Mac projects and Photoshop use PostScript face names. Match installed
    // family/style pairs where Windows exposes the same face with spaces.
    if(!families.contains(style.fontName,Qt::CaseInsensitive)){
        const auto key=fontKey(style.fontName);bool found=false;
        for(const auto& family:families){if(fontKey(family)==key){font.setFamily(family);found=true;break;}
            for(const auto& face:QFontDatabase::styles(family))if(fontKey(family+face)==key){font=QFontDatabase::font(family,face,12);found=true;break;}if(found)break;}
    }
    font.setPointSizeF(style.fontSize*72./96.);font.setLetterSpacing(QFont::AbsoluteSpacing,style.tracking);font.setHintingPreference(QFont::PreferNoHinting);return font;
}
QSizeF textBoxSize(const TextStyle& style){auto device=metricsDevice();return makeLayout(style,&device).size;}
double textFirstBaseline(const TextStyle& style){require(style);auto device=metricsDevice();return TextStyle::padding+style.lineHeight()-QFontMetricsF(textFont(style),&device).descent();}
std::shared_ptr<const Raster> textRaster(const TextStyle& style){
    auto device=metricsDevice();auto layout=makeLayout(style,&device);const int width=int(std::ceil(layout.size.width())),height=int(std::ceil(layout.size.height()));
    QImage image(width,height,QImage::Format_RGBA8888_Premultiplied);if(image.isNull())throw std::runtime_error("Not enough memory to render text");image.setDotsPerMeterX(3780);image.setDotsPerMeterY(3780);image.fill(Qt::transparent);
    {QPainter painter(&image);painter.setRenderHints(QPainter::Antialiasing|QPainter::TextAntialiasing);painter.setPen(QColor::fromRgbF(style.red,style.green,style.blue));painter.setClipRect(QRectF(TextStyle::padding,TextStyle::padding,std::max(1.,width-2*TextStyle::padding),std::max(1.,height-2*TextStyle::padding)));for(const auto& paragraph:layout.paragraphs)paragraph->draw(&painter,{});}
    return Raster::fromRgba(width,height,image.constBits(),size_t(image.bytesPerLine()));
}
std::string textLayerName(const QString& content){auto name=content.simplified();if(name.isEmpty())return "Text";if(name.size()>40){name=name.left(40);if(name.back().isHighSurrogate())name.chop(1);}return name.toUtf8().toStdString();}
Layer createTextLayer(Point origin,const TextStyle& style){
    Layer result;result.id=newId();result.name=textLayerName(style.content);result.raster=textRaster(style);result.textJson=encodeTextStyle(style);result.transform={origin.x,origin.y,double(result.raster->width),double(result.raster->height)};if(!result.transform.valid())throw std::runtime_error("Invalid text position");return result;
}
Layer restyleText(const Layer& source,const TextStyle& style){
    require(style);if(!source.raster||source.textJson.empty())throw std::runtime_error("Layer is no longer editable text");
    if(decodeTextStyle(source.textJson)==style)return source;
    auto result=source;result.raster=textRaster(style);result.textJson=encodeTextStyle(style);
    const auto anchor=source.transform.fromUnit({0,0});result.transform.width*=double(result.raster->width)/source.raster->width;result.transform.height*=double(result.raster->height)/source.raster->height;
    const auto moved=result.transform.fromUnit({0,0});result.transform.x+=anchor.x-moved.x;result.transform.y+=anchor.y-moved.y;
    if(!result.transform.valid())throw std::runtime_error("Text transform exceeds its limit");if(result.mask&&!result.mask->placement)result.mask->placement=source.transform;return result;
}
}
