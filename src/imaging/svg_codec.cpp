#include "svg_codec.h"
#include <QFile>
#include <QImage>
#include <QPainter>
#include <QSvgRenderer>
#include <QXmlStreamReader>
#include <QRegularExpression>
#include <cmath>
namespace compositor::imaging {
namespace {
void validateSvgResources(const QByteArray& data){
    auto allowed=[](QString value){value=value.trimmed();if(value.isEmpty()||value.startsWith('#'))return true;static const QRegularExpression embedded("^data:image/(png|jpe?g|gif|webp|bmp);base64,",QRegularExpression::CaseInsensitiveOption);return embedded.match(value).hasMatch();};
    static const QRegularExpression url("url\\(\\s*(['\"]?)(.*?)\\1\\s*\\)",QRegularExpression::CaseInsensitiveOption);
    auto css=[&](const QString& value){if(value.contains("@import",Qt::CaseInsensitive))throw std::runtime_error("SVG external stylesheets are unsupported; embed the artwork in the SVG");auto matches=url.globalMatch(value);while(matches.hasNext())if(!allowed(matches.next().captured(2)))throw std::runtime_error("SVG external resources are unsupported; embed the artwork in the SVG");};
    QXmlStreamReader xml(data);while(!xml.atEnd()){const auto token=xml.readNext();if(token==QXmlStreamReader::DTD||token==QXmlStreamReader::EntityReference)throw std::runtime_error("SVG document types and external entities are unsupported");if(token==QXmlStreamReader::StartElement){for(const auto& attribute:xml.attributes()){const auto name=attribute.name();const auto value=attribute.value().toString();if((name=="href"||name=="src"||name=="base")&&!allowed(value))throw std::runtime_error("SVG external resources are unsupported; embed the artwork in the SVG");css(value);}}else if(token==QXmlStreamReader::Characters)css(xml.text().toString());}if(xml.hasError())throw std::runtime_error("Invalid SVG XML");
}
}
DecodedImage decodeSvg(const std::filesystem::path& path,std::optional<std::pair<int,int>> fitting,const ImportOptions& options){
    QFile file(QString::fromStdWString(path.wstring()));if(!file.open(QIODevice::ReadOnly)||file.size()>64000000)throw std::runtime_error("SVG is unreadable or exceeds the 64 MB input budget");
    const auto data=file.readAll();checkCancelled(options);validateSvgResources(data);QSvgRenderer svg(data);if(!svg.isValid())throw std::runtime_error("Invalid or unsupported SVG");
    auto size=svg.defaultSize();if(size.isEmpty())size=svg.viewBoxF().size().toSize();if(size.isEmpty())throw std::runtime_error("SVG has no positive dimensions");
    if(fitting){double scale=std::min(double(fitting->first)/size.width(),double(fitting->second)/size.height());size={std::max(1,int(std::round(size.width()*scale))),std::max(1,int(std::round(size.height()*scale)))};}
    checkedBytes(unsigned(size.width()),unsigned(size.height()),4,options);QImage image(size,QImage::Format_RGBA8888_Premultiplied);if(image.isNull())throw std::bad_alloc();image.fill(Qt::transparent);QPainter painter(&image);svg.render(&painter);painter.end();checkCancelled(options);
    DecodedImage result;result.image={unsigned(size.width()),unsigned(size.height()),size_t(image.bytesPerLine()),{image.constBits(),image.constBits()+image.sizeInBytes()}};result.metadata.decoder="Qt SVG 6.8.3";result.metadata.profileStatus=ProfileStatus::DeclaredSrgb;return result;
}
}
