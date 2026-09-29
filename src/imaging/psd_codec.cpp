// Photoshop reader informed by PSDReader.swift at Compositor v1.2.11,
// Copyright (c) 2026 Wonder Assembly LLC, MIT (graphics/upstream/LICENSE).
#include "psd_codec.h"
#include "psd_metadata.h"
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <zlib.h>
#include <algorithm>
#include <cstring>
#include <limits>
#include <map>
#include <set>
namespace compositor::imaging {
namespace {
struct Reader {
    std::span<const uint8_t> data;size_t at{};
    void need(uint64_t n)const{if(n>data.size()||at>data.size()-size_t(n))throw std::runtime_error("Photoshop file is truncated or damaged");}
    void skip(uint64_t n){need(n);at+=size_t(n);}
    uint8_t u8(){need(1);return data[at++];}
    uint16_t u16(){const auto a=u8();return uint16_t(a<<8|u8());}
    int16_t i16(){return int16_t(u16());}
    uint32_t u32(){const auto a=u16();return uint32_t(a)<<16|u16();}
    int32_t i32(){return int32_t(u32());}
    uint64_t u64(){const auto a=u32();return uint64_t(a)<<32|u32();}
    uint64_t length(bool large){return large?u64():u32();}
    std::span<const uint8_t> bytes(uint64_t n){need(n);auto value=data.subspan(at,size_t(n));at+=size_t(n);return value;}
    QByteArray key(){auto v=bytes(4);return QByteArray(reinterpret_cast<const char*>(v.data()),4);}
    Reader section(uint64_t n){return {bytes(n)};}
};
struct Box {int64_t x{},y{},right{},bottom{};int64_t width()const{return std::max<int64_t>(0,right-x);}int64_t height()const{return std::max<int64_t>(0,bottom-y);}bool operator==(const Box&)const=default;};
Box box(Reader&r){const auto top=r.i32(),left=r.i32(),bottom=r.i32(),right=r.i32();return {left,top,right,bottom};}
struct Record {Layer layer;Box source,target,maskSource,maskTarget;std::vector<std::pair<int,uint64_t>> channels;PhotoshopExtras extra;QByteArray blend;int section{};bool advancedBlend{},clipping{},maskDisabled{},maskLinked{true},maskFromRender{},hasMask{},cropped{};uint8_t maskDefault{255},fill{255};};
QString unicode(Reader& r){const auto n=r.u32();if(n>16384)throw std::runtime_error("Photoshop layer name exceeds budget");QString s;s.reserve(n);for(uint32_t i=0;i<n;++i)s.append(QChar(r.u16()));while(s.endsWith(QChar(0)))s.chop(1);return s;}
Record record(Reader& r,bool large,uint64_t& metadataBytes,const ImportOptions& options){
    Record v;v.layer.id=newId();v.source=v.target=box(r);const auto channels=r.u16();if(channels>56)throw std::runtime_error("Too many Photoshop channels");
    for(unsigned i=0;i<channels;++i){const auto id=r.i16();const auto length=r.length(large);v.channels.emplace_back(id,length);}
    if(r.key()!="8BIM")throw std::runtime_error("Invalid Photoshop layer record");v.blend=r.key();v.layer.opacity=r.u8()/255.;v.clipping=r.u8()!=0;v.layer.visible=(r.u8()&2)==0;r.skip(1);
    auto extra=r.section(r.u32());auto mask=extra.section(extra.u32());
    if(mask.data.size()>=18){v.hasMask=true;v.maskSource=v.maskTarget=box(mask);v.maskDefault=mask.u8();const auto flags=mask.u8();v.maskDisabled=flags&2;v.maskLinked=!(flags&1);v.maskFromRender=flags&8;}
    extra.skip(extra.u32());const auto nameLength=extra.u8();const auto name=extra.bytes(nameLength);v.layer.name=QString::fromLatin1(reinterpret_cast<const char*>(name.data()),qsizetype(name.size())).toUtf8().toStdString();extra.skip((4-(nameLength+1)%4)%4);
    static const std::set<QByteArray> longKeys{"LMsk","Lr16","Lr32","Layr","Mt16","Mt32","Mtrn","Alph","FMsk","lnk2","FEid","FXid","PxSD"};
    while(extra.at+12<=extra.data.size()){
        auto signature=extra.key();if(signature!="8BIM"&&signature!="8B64")break;auto key=extra.key();const auto length=extra.length(signature=="8B64"||(large&&longKeys.contains(key)));auto payload=extra.bytes(length);if(length&1)extra.skip(1);
        // Only retain editable metadata, never linked smart-object contents.
        static const std::set<QByteArray> editable{"TySh","tySh","vmsk","vsms","vogk","SoCo","vstk","levl","curv","hue2","hue "};const auto metadataLimit=std::min<uint64_t>(64000000,options.maxWorkingBytes/8);if(editable.contains(key)&&length<=8000000&&length<=metadataLimit-metadataBytes){v.extra.insert(key,QByteArray(reinterpret_cast<const char*>(payload.data()),qsizetype(length)));metadataBytes+=length;}else v.extra.insert(key,{}); // Retain the conversion category without linked or oversized contents.
        Reader value{payload};if(key=="luni")v.layer.name=unicode(value).toUtf8().toStdString();else if(key=="iOpa"&&length)v.fill=value.u8();else if((key=="lsct"||key=="lsdk")&&length>=4)v.section=int(value.u32());else if(key=="tsly"&&length)v.advancedBlend=value.u8()==0;
    }
    if(v.layer.name.empty())v.layer.name="Layer";if(v.layer.name.size()>16384)throw std::runtime_error("Photoshop layer name exceeds budget");return v;
}
Box clipped(Box b,int width,int height){const auto left=std::clamp<int64_t>(b.x,0,width),top=std::clamp<int64_t>(b.y,0,height);return {left,top,std::max(left,std::clamp<int64_t>(b.right,0,width)),std::max(top,std::clamp<int64_t>(b.bottom,0,height))};}
Box maskGrid(const Record& v,const ImportOptions& options){auto result=v.maskTarget;if(v.target.width()&&v.target.height()){result={std::min(v.target.x,result.x),std::min(v.target.y,result.y),std::max(v.target.right,result.right),std::max(v.target.bottom,result.bottom)};}const auto pad=std::max<int64_t>(1,(std::max(result.width(),result.height())+89)/90);if(result.width()+2*pad<=options.maxSide){result.x-=pad;result.right+=pad;}if(result.height()+2*pad<=options.maxSide){result.y-=pad;result.bottom+=pad;}return result;}
bool fits(const std::vector<Record>& records,const ImportOptions& options){
    uint64_t images=0,masks=0,transient=0;for(const auto& v:records){const auto imagePixels=uint64_t(v.target.width())*v.target.height();if(v.target.width()>options.maxSide||v.target.height()>options.maxSide||imagePixels>limits::surfacePixels||imagePixels>options.remainingPixels-images)return false;images+=imagePixels;
        uint64_t sourceMaskPixels=0;if(v.hasMask&&!v.maskFromRender&&v.maskTarget.width()&&v.maskTarget.height()){const auto grid=maskGrid(v,options);sourceMaskPixels=uint64_t(v.maskTarget.width())*v.maskTarget.height();const auto maskPixels=uint64_t(grid.width())*grid.height();if(v.maskTarget.width()>options.maxSide||v.maskTarget.height()>options.maxSide||grid.width()>options.maxSide||grid.height()>options.maxSide||sourceMaskPixels>limits::surfacePixels||maskPixels>limits::surfacePixels||maskPixels>options.remainingMaskPixels-masks)return false;masks+=maskPixels;}
        transient=std::max(transient,imagePixels*8+sourceMaskPixels);
    }return images*4+masks+transient<=options.maxWorkingBytes;
}
std::vector<uint8_t> plane(Reader input,Box source,Box target,bool large,const ImportOptions& options){
    const auto mode=input.u16();const auto sw=source.width(),sh=source.height(),tw=target.width(),th=target.height();
    if(!tw||!th)return {};if(sw<=0||sh<=0||sw>30000000||sh>30000000)throw std::runtime_error("Photoshop channel dimensions exceed decode budget");
    checkedBytes(uint32_t(tw),uint32_t(th),1,options);const auto left=target.x-source.x,top=target.y-source.y;if(left<0||top<0||left+tw>sw||top+th>sh)throw std::runtime_error("Invalid Photoshop crop bounds");
    std::vector<uint8_t> result(size_t(tw*th)),row(size_t(sw),uint8_t{});
    auto store=[&](int64_t y){if(y>=top&&y<top+th)std::copy_n(row.begin()+left,tw,result.begin()+(y-top)*tw);};
    if(mode==0){input.need(uint64_t(sw)*sh);for(int64_t y=top;y<top+th;++y){checkCancelled(options);const auto bytes=input.data.subspan(input.at+size_t(y*sw+left),size_t(tw));std::copy(bytes.begin(),bytes.end(),result.begin()+(y-top)*tw);}return result;}
    if(mode==1){std::vector<uint32_t> lengths;lengths.reserve(size_t(sh));for(int64_t y=0;y<sh;++y)lengths.push_back(large?input.u32():input.u16());for(int64_t y=0;y<sh;++y){auto r=input.section(lengths[size_t(y)]);if(y<top||y>=top+th)continue;checkCancelled(options);size_t x=0;while(r.at<r.data.size()&&x<size_t(sw)){const auto n=int8_t(r.u8());if(n>=0){const auto bytes=r.bytes(size_t(n)+1);if(bytes.size()>row.size()-x)throw std::runtime_error("Invalid Photoshop RLE row");std::copy(bytes.begin(),bytes.end(),row.begin()+x);x+=bytes.size();}else if(n!=-128){const auto count=size_t(1-int(n));if(count>row.size()-x)throw std::runtime_error("Invalid Photoshop RLE run");std::fill_n(row.begin()+x,count,r.u8());x+=count;}}if(x!=size_t(sw))throw std::runtime_error("Truncated Photoshop RLE row");store(y);}return result;}
    if(mode!=2&&mode!=3)throw std::runtime_error("Photoshop channel compression is unsupported");
    z_stream z{};if(inflateInit(&z)!=Z_OK)throw std::bad_alloc();struct End{z_stream* z;~End(){inflateEnd(z);}}end{&z};size_t offset=input.at;int status=Z_OK;
    for(int64_t y=0;y<sh;++y){checkCancelled(options);z.next_out=row.data();z.avail_out=uInt(row.size());while(z.avail_out){if(!z.avail_in){const auto n=std::min<size_t>(input.data.size()-offset,1<<20);if(!n)throw std::runtime_error("Truncated Photoshop ZIP channel");z.next_in=const_cast<Bytef*>(input.data.data()+offset);z.avail_in=uInt(n);offset+=n;}status=inflate(&z,Z_NO_FLUSH);if(status!=Z_OK&&status!=Z_STREAM_END)throw std::runtime_error("Damaged Photoshop ZIP channel");if(status==Z_STREAM_END&&z.avail_out)throw std::runtime_error("Truncated Photoshop ZIP row");}if(mode==3)for(size_t x=1;x<row.size();++x)row[x]=uint8_t(row[x]+row[x-1]);store(y);}
    while(status!=Z_STREAM_END){uint8_t excess{};z.next_out=&excess;z.avail_out=1;if(!z.avail_in){const auto n=std::min<size_t>(input.data.size()-offset,1<<20);if(!n)throw std::runtime_error("Photoshop ZIP checksum is missing");z.next_in=const_cast<Bytef*>(input.data.data()+offset);z.avail_in=uInt(n);offset+=n;}status=inflate(&z,Z_NO_FLUSH);if(z.avail_out!=1||(status!=Z_OK&&status!=Z_STREAM_END))throw std::runtime_error("Invalid Photoshop ZIP channel length or checksum");}
    return result;
}
void pixels(Reader& r,Record& v,bool large,const ImportOptions& options){
    std::map<int,std::vector<uint8_t>> planes;for(const auto& [id,length]:v.channels){auto input=r.section(length);if(id<-2||id>2||length<2)continue;auto limits=options;if(id==-2)limits.remainingPixels=options.remainingMaskPixels;planes[id]=plane(input,id==-2?v.maskSource:v.source,id==-2?v.maskTarget:v.target,large,limits);}
    const auto w=int(v.target.width()),h=int(v.target.height());if(w>0&&h>0){std::vector<uint8_t> bytes(size_t(w)*h*4);for(size_t i=0;i<size_t(w)*h;++i){const auto a=planes[-1].empty()?255:planes[-1][i];for(int c=0;c<3;++c)bytes[i*4+c]=planes[c].empty()?0:uint8_t((unsigned(planes[c][i])*a+127)/255);bytes[i*4+3]=uint8_t(a);}v.layer.raster=Raster::fromRgba(w,h,bytes.data(),size_t(w)*4);v.layer.transform={double(v.target.x),double(v.target.y),double(w),double(h)};}
    if(v.hasMask&&!v.maskFromRender&&!planes[-2].empty()){
        auto gray=std::make_shared<GrayRaster>();gray->width=int(v.maskTarget.width());gray->height=int(v.maskTarget.height());gray->pixels=std::move(planes[-2]);
        // Photoshop records an explicit exterior. Native project masks infer it
        // from the thumbnail edge, so retain both black and white defaults in a
        // padded union of the authored mask and layer bounds. The proportional
        // border survives thumbnail sampling when this mask is saved/reopened.
        Mask mask;mask.raster=gray;mask.enabled=!v.maskDisabled;mask.linked=v.maskLinked;mask.placement=Transform{double(v.maskTarget.x),double(v.maskTarget.y),double(gray->width),double(gray->height)};
        const auto grid=maskGrid(v,options);auto maskLimits=options;maskLimits.remainingPixels=options.remainingMaskPixels;checkedBytes(uint32_t(grid.width()),uint32_t(grid.height()),1,maskLimits);auto expanded=std::make_shared<GrayRaster>();expanded->width=int(grid.width());expanded->height=int(grid.height());expanded->pixels.assign(size_t(expanded->width)*expanded->height,v.maskDefault);for(int yy=0;yy<gray->height;++yy)std::copy_n(gray->pixels.begin()+size_t(yy)*gray->width,gray->width,expanded->pixels.begin()+size_t(v.maskTarget.y-grid.y+yy)*expanded->width+size_t(v.maskTarget.x-grid.x));mask.raster=expanded;mask.placement=Transform{double(grid.x),double(grid.y),double(expanded->width),double(expanded->height)};
        v.layer.mask=std::move(mask);
    }
}
Blend blend(const QByteArray& key,bool& known){
    // Resolve by names so the canonical enum order can evolve independently.
    static const std::map<QByteArray,std::string> names{{"norm","Normal"},{"mul ","Multiply"},{"scrn","Screen"},{"over","Overlay"},{"sLit","Soft Light"},{"dark","Darken"},{"lite","Lighten"},{"diff","Difference"},{"div ","Color Dodge"},{"idiv","Color Burn"},{"hue ","Hue"},{"sat ","Saturation"},{"colr","Color"},{"lum ","Luminosity"},{"lbrn","Linear Burn"},{"lddg","Linear Dodge (Add)"},{"hLit","Hard Light"},{"vLit","Vivid Light"},{"lLit","Linear Light"},{"pLit","Pin Light"},{"hMix","Hard Mix"},{"smud","Exclusion"},{"fsub","Subtract"},{"fdiv","Divide"}};
    auto it=names.find(key);if(it!=names.end())for(size_t i=0;i<blendNames.size();++i)if(it->second==blendNames[i]){known=true;return Blend(i);}known=key=="pass";return Blend::Normal;
}
}
PhotoshopImport decodePhotoshop(std::span<const uint8_t> bytes,const ImportOptions& options){
    checkCancelled(options);Reader r{bytes};if(r.key()!="8BPS")throw std::runtime_error("Not a Photoshop file");const auto version=r.u16();if(version!=1&&version!=2)throw std::runtime_error("Unsupported Photoshop file version");const bool large=version==2;r.skip(6);const auto channelCount=r.u16();const auto height=r.u32(),width=r.u32();const auto depth=r.u16(),mode=r.u16();if(depth!=8||mode!=3)throw std::runtime_error("Only 8-bit RGB Photoshop files can be imported");checkedBytes(width,height,1,options);
    PhotoshopImport result;auto& doc=result.document;doc.id=newId();doc.width=int(width);doc.height=int(height);r.skip(r.u32());auto resources=r.section(r.u32());
    while(resources.at+12<=resources.data.size()){if(resources.key()!="8BIM")break;const auto id=resources.u16();const auto n=resources.u8();resources.skip(n);if((n+1)&1)resources.skip(1);const auto length=resources.u32();auto data=resources.section(length);if(id==1005&&length>=4)doc.resolution=std::clamp(data.u32()/65536.,1.,9600.);if(length&1)resources.skip(1);}
    auto section=r.section(r.length(large));std::vector<Record> records;uint64_t metadataBytes=0;
    if(section.data.size()>=(large?8U:4U)){auto info=section.section(section.length(large));if(info.data.size()>=2){const auto count=std::abs(int(info.i16()));if(count>10000)throw std::runtime_error("Photoshop layer count exceeds budget");for(int i=0;i<count;++i){checkCancelled(options);records.push_back(record(info,large,metadataBytes,options));}if(!fits(records,options)){for(auto& v:records){v.target=clipped(v.source,int(width),int(height));v.maskTarget=clipped(v.maskSource,int(width),int(height));v.cropped=v.target!=v.source||v.maskTarget!=v.maskSource;}if(!fits(records,options))throw std::runtime_error("Photoshop layers exceed the document pixel budget after cropping to canvas");}uint64_t used=0,usedMasks=0;for(auto& v:records){auto remaining=options;remaining.remainingPixels-=used;remaining.remainingMaskPixels-=usedMasks;pixels(info,v,large,remaining);if(v.layer.raster)used+=uint64_t(v.layer.raster->width)*v.layer.raster->height;if(v.layer.mask)usedMasks+=uint64_t(v.layer.mask->raster->width)*v.layer.mask->raster->height;}}}
    if(records.empty()){
        if(channelCount<3||channelCount>56)throw std::runtime_error("Unsupported Photoshop merged channel count");const auto compression=r.u16();if(compression>3)throw std::runtime_error("Unsupported Photoshop merged compression");std::vector<std::vector<uint8_t>> channels;std::vector<uint32_t> rowLengths;if(compression==1){for(uint64_t i=0;i<uint64_t(channelCount)*height;++i)rowLengths.push_back(large?r.u32():r.u16());}
        if(compression<2)for(unsigned c=0;c<channelCount;++c){checkCancelled(options);std::vector<uint8_t> data;data.push_back(0);data.push_back(uint8_t(compression));if(compression==0){const auto raw=r.bytes(uint64_t(width)*height);data.insert(data.end(),raw.begin(),raw.end());}else{uint64_t total=0;for(unsigned y=0;y<height;++y){const auto length=rowLengths[size_t(c)*height+y];total+=length;if(large){data.push_back(uint8_t(length>>24));data.push_back(uint8_t(length>>16));}data.push_back(uint8_t(length>>8));data.push_back(uint8_t(length));}auto raw=r.bytes(total);data.insert(data.end(),raw.begin(),raw.end());}if(c<4)channels.push_back(plane({data},{0,0,width,height},{0,0,width,height},large,options));}
        else{
            // The merged image has one ZIP stream across all planar channels.
            // Inflate row by row; extra spot channels never require an image allocation.
            z_stream stream{};if(inflateInit(&stream)!=Z_OK)throw std::bad_alloc();struct End{z_stream* z;~End(){inflateEnd(z);}}end{&stream};size_t offset=r.at;int status=Z_OK;std::vector<uint8_t> row(width);auto read=[&](uint8_t* target,size_t count){stream.next_out=target;stream.avail_out=uInt(count);while(stream.avail_out){if(!stream.avail_in){const auto n=std::min<size_t>(r.data.size()-offset,1<<20);if(!n)throw std::runtime_error("Truncated Photoshop merged ZIP stream");stream.next_in=const_cast<uint8_t*>(r.data.data()+offset);stream.avail_in=uInt(n);offset+=n;}status=inflate(&stream,Z_NO_FLUSH);if(status!=Z_OK&&status!=Z_STREAM_END)throw std::runtime_error("Damaged Photoshop merged ZIP stream");if(status==Z_STREAM_END&&stream.avail_out)throw std::runtime_error("Truncated Photoshop merged ZIP data");}};
            for(unsigned c=0;c<channelCount;++c){if(c<4)channels.emplace_back(size_t(width)*height);for(unsigned y=0;y<height;++y){checkCancelled(options);read(row.data(),row.size());if(compression==3)for(size_t x=1;x<row.size();++x)row[x]=uint8_t(row[x]+row[x-1]);if(c<4)std::copy(row.begin(),row.end(),channels[c].begin()+size_t(y)*width);}}
            while(status!=Z_STREAM_END){uint8_t byte;stream.next_out=&byte;stream.avail_out=1;if(!stream.avail_in){const auto n=std::min<size_t>(r.data.size()-offset,1<<20);if(!n)throw std::runtime_error("Photoshop merged ZIP checksum is missing");stream.next_in=const_cast<uint8_t*>(r.data.data()+offset);stream.avail_in=uInt(n);offset+=n;}status=inflate(&stream,Z_NO_FLUSH);if(stream.avail_out!=1||(status!=Z_OK&&status!=Z_STREAM_END))throw std::runtime_error("Invalid Photoshop merged ZIP length or checksum");}
        }
        std::vector<uint8_t> rgba(checkedBytes(width,height,4,options));for(size_t i=0;i<size_t(width)*height;++i){const unsigned a=channels.size()>3?channels[3][i]:255;for(size_t c=0;c<3;++c)rgba[i*4+c]=uint8_t((channels[c][i]*a+127)/255);rgba[i*4+3]=uint8_t(a);}Layer layer;layer.id=newId();layer.name="Background";layer.raster=Raster::fromRgba(int(width),int(height),rgba.data(),size_t(width)*4);layer.transform={0,0,double(width),double(height)};doc.layers.push_back(std::move(layer));return result;
    }
    std::vector<std::string> groups;std::map<std::string,std::string> bases;
    // The upstream PSD contract stores section divider, children, then folder.
    // Accept reversed folder records too, as emitted by several PSD writers.
    const auto firstFolder=std::find_if(records.begin(),records.end(),[](const Record& v){return v.section!=0;});if(firstFolder!=records.end()&&firstFolder->section!=3)std::reverse(records.begin(),records.end());
    for(auto& v:records){if(v.section==3){groups.push_back(newId());continue;}const bool group=v.section==1||v.section==2;auto& layer=v.layer;if(group){if(groups.empty())throw std::runtime_error("Unbalanced Photoshop folder records");layer.id=groups.back();groups.pop_back();layer.group=true;layer.raster.reset();layer.transform={0,0,double(width),double(height)};}layer.parentId=groups.empty()?"":groups.back();bool known=false;layer.blend=blend(v.blend,known);auto note=[&](const QString& text){result.conversions.append(QString::fromStdString(layer.name)+": "+text);};if(group){layer.blend=Blend::Normal;if(v.blend!="pass"&&v.blend!="norm")note("Folder blend mode was converted to pass-through.");}else if(!known)note("Unsupported Photoshop blend mode was converted to Normal.");
        if(v.cropped)note("Cropped to the canvas to fit the document budget; pixels outside the canvas were omitted.");if(v.advancedBlend)note("Photoshop Transparency Shapes Layer was omitted; advanced blend appearance may differ.");if(v.fill!=255&&layer.blend!=Blend::Normal)note("Photoshop fill opacity was combined with layer opacity; the blend appearance may differ.");
        bool effects=v.extra.contains("lfx2")||v.extra.contains("lrFX")||v.extra.contains("lmfx");if(!effects)layer.opacity*=v.fill/255.;else note("Photoshop layer effects were discarded; appearance may differ.");
        if(!layer.raster&&!layer.group)layer.transform={0,0,double(width),double(height)};applyPhotoshopMetadata(layer,v.extra,int(width),int(height),result.conversions,options);
        if(layer.name.empty())continue;
        if(v.clipping){auto base=bases[layer.parentId];if(!base.empty())layer.maskSourceId=base;else note("Clipping was skipped because its base is unsupported.");}else bases[layer.parentId]=!group&&layer.adjustmentJson.empty()?layer.id:"";
        doc.layers.push_back(std::move(layer));
    }
    if(!groups.empty())throw std::runtime_error("Unbalanced Photoshop folders");validateDocument(doc);return result;
}
PhotoshopImport decodePhotoshop(const std::filesystem::path& path,const ImportOptions& options){QFile file(QString::fromStdWString(path.wstring()));if(!file.open(QIODevice::ReadOnly)||file.size()<26)throw std::runtime_error("Cannot open Photoshop file");auto* bytes=file.map(0,file.size());if(!bytes)throw std::runtime_error("Cannot map Photoshop file");auto result=decodePhotoshop({bytes,size_t(file.size())},options);if(result.document.layers.size()==1&&result.document.layers[0].name=="Background")result.document.layers[0].name=QFileInfo(file).completeBaseName().toUtf8().toStdString();return result;}
}
