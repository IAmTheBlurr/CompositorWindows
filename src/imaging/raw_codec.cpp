#include "raw_codec.h"
#include <libraw/libraw.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <set>
#include <cwctype>
namespace compositor::imaging {
namespace {
void checked(int code){if(code!=LIBRAW_SUCCESS)throw std::runtime_error(std::string("Camera RAW: ")+libraw_strerror(code));}
std::array<double,3> kelvin(double temperature){
    const double t=std::clamp(temperature,2000.,50000.)/100;
    const double r=t<=66?255:329.698727446*std::pow(t-60,-.1332047592);
    const double g=t<=66?99.4708025861*std::log(t)-161.1195681661:288.1221695283*std::pow(t-60,-.0755148492);
    const double b=t>=66?255:t<=19?0:138.5177312231*std::log(t-10)-305.0447927307;
    return {std::clamp(r,1.,255.)/255,std::clamp(g,1.,255.)/255,std::clamp(b,1.,255.)/255};
}
}
struct RawFrame::Impl { LibRaw decoder;ImportOptions options;RawDevelopSettings initial;std::array<float,4> camera{};bool unpacked{}; };
RawFrame::RawFrame(const std::filesystem::path& path,const ImportOptions& options):impl_(std::make_unique<Impl>()){
    auto& p=*impl_;p.options=options;checkCancelled(options);
    p.decoder.imgdata.rawparams.max_raw_memory_mb=unsigned(std::min<uint64_t>(options.maxWorkingBytes/(1024*1024),1536));
    p.decoder.set_progress_handler([](void* data,enum LibRaw_progress,int,int){auto* p=static_cast<Impl*>(data);return p->options.cancelled&&p->options.cancelled()?1:0;},&p);
    checked(p.decoder.open_file(path.c_str()));
    checkedBytes(p.decoder.imgdata.sizes.width,p.decoder.imgdata.sizes.height,4,options);
    for(size_t i=0;i<4;++i)p.camera[i]=p.decoder.imgdata.color.cam_mul[i];
    if(p.camera[0]<=0||p.camera[1]<=0||p.camera[2]<=0){for(size_t i=0;i<4;++i)p.camera[i]=p.decoder.imgdata.color.pre_mul[i];}
    if(p.camera[3]<=0)p.camera[3]=p.camera[1];
    // Estimate a useful Kelvin anchor from the as-shot red/blue ratio. The
    // actual camera multipliers remain exact while As Shot is selected.
    double best=1e20;for(int t=2000;t<=15000;t+=25){const auto c=kelvin(t);const double error=std::abs(std::log(c[2]/c[0])-std::log(std::max(.001,double(p.camera[0]))/std::max(.001,double(p.camera[2]))));if(error<best){best=error;p.initial.temperature=t;}}
}
RawFrame::~RawFrame()=default;
RawDevelopSettings RawFrame::settings()const{return impl_->initial;}
bool RawFrame::matches(const std::filesystem::path& path){auto ext=path.extension().wstring();std::transform(ext.begin(),ext.end(),ext.begin(),[](wchar_t c){return std::towlower(c);});static const std::set<std::wstring> formats{L".3fr",L".ari",L".arw",L".bay",L".cap",L".cr2",L".cr3",L".crw",L".dcr",L".dcs",L".dng",L".drf",L".eip",L".erf",L".fff",L".gpr",L".iiq",L".k25",L".kdc",L".mdc",L".mef",L".mos",L".mrw",L".nef",L".nrw",L".obm",L".orf",L".pef",L".ptx",L".pxn",L".r3d",L".raf",L".raw",L".rw2",L".rwl",L".rwz",L".sr2",L".srf",L".srw",L".x3f"};return formats.contains(ext);}
DecodedImage RawFrame::develop(const RawDevelopSettings& settings,bool preview,const ImportOptions& options){
    auto& p=*impl_;p.options=options;checkCancelled(options);
    if(!std::isfinite(settings.exposure)||settings.exposure < -5||settings.exposure>5||!std::isfinite(settings.temperature)||settings.temperature<2000||settings.temperature>50000||!std::isfinite(settings.tint)||std::abs(settings.tint)>150||!std::isfinite(settings.boost)||settings.boost<0||settings.boost>1)throw std::runtime_error("Invalid RAW development settings");
    if(!p.unpacked){checked(p.decoder.unpack());p.unpacked=true;}
    auto& out=p.decoder.imgdata.params;out.output_bps=8;out.output_color=1;out.no_auto_bright=1;out.use_camera_wb=settings.asShot?1:0;out.use_auto_wb=0;out.half_size=preview?1:0;out.user_qual=preview?0:3;
    out.exp_correc=1;out.exp_shift=float(std::exp2(settings.exposure));out.exp_preser=1;
    out.gamm[0]=1/(1+settings.boost*1.4);out.gamm[1]=1+settings.boost*11.92;
    std::fill(std::begin(out.user_mul),std::end(out.user_mul),0.f);
    if(!settings.asShot){const auto neutral=kelvin(p.initial.temperature),chosen=kelvin(settings.temperature);for(int i=0;i<3;++i)out.user_mul[i]=float(std::max(.001,double(p.camera[size_t(i)]))*neutral[size_t(i)]/chosen[size_t(i)]*(i==1?std::exp(-settings.tint/300):std::exp(settings.tint/600)));out.user_mul[3]=out.user_mul[1];}
    checked(p.decoder.dcraw_process());checkCancelled(options);int code=0;auto* buffer=p.decoder.dcraw_make_mem_image(&code);checked(code);if(!buffer)throw std::runtime_error("RAW development produced no image");
    std::unique_ptr<libraw_processed_image_t,decltype(&LibRaw::dcraw_clear_mem)> owner(buffer,&LibRaw::dcraw_clear_mem);
    if(buffer->type!=LIBRAW_IMAGE_BITMAP||buffer->bits!=8||buffer->colors<3)throw std::runtime_error("Unsupported RAW output layout");
    DecodedImage result;result.image.width=buffer->width;result.image.height=buffer->height;result.image.stride=size_t(buffer->width)*4;result.image.pixels.resize(checkedBytes(buffer->width,buffer->height,4,options));
    for(size_t i=0;i<size_t(buffer->width)*buffer->height;++i){if((i&65535)==0)checkCancelled(options);for(size_t c=0;c<3;++c)result.image.pixels[i*4+c]=buffer->data[i*buffer->colors+c];result.image.pixels[i*4+3]=255;}
    result.metadata.decoder="LibRaw 0.21.5";result.metadata.profileStatus=ProfileStatus::DeclaredSrgb;return result;
}
}
