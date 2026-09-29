#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "onnx_subject_provider.h"
#include "subject_input.h"
#define ORT_API_MANUAL_INIT
#include <onnxruntime_cxx_api.h>
#undef ORT_API_MANUAL_INIT
#include <windows.h>
#include <bcrypt.h>
#include <array>
#include <cmath>
#include <fstream>
#include <thread>
#include <chrono>
#include <mutex>
#include <QImage>
namespace compositor::imaging {
namespace {
void initializeRuntime(){
    // Resolve once on the first inference request, before any Ort wrapper member
    // can dereference its API table. A system DLL may be older than our headers.
    static std::once_flag initialized;
    std::call_once(initialized,[]{
        const auto* base=OrtGetApiBase();const auto* api=base?base->GetApi(ORT_API_VERSION):nullptr;
        if(!api){
            std::array<wchar_t,32768> path{};const auto module=GetModuleHandleW(L"onnxruntime.dll");
            const DWORD length=module?GetModuleFileNameW(module,path.data(),DWORD(path.size())):0;
            const auto utf8=length?std::filesystem::path(std::wstring(path.data(),length)).u8string():std::u8string(u8"unknown module");
            const std::string loaded(utf8.begin(),utf8.end());
            throw std::runtime_error("ONNX Runtime API "+std::to_string(ORT_API_VERSION)+" is unavailable; loaded "+(base?base->GetVersionString():"unknown version")+" from "+loaded);
        }
        Ort::InitApi(api);
    });
}
std::string hash(const std::filesystem::path& file){
    BCRYPT_ALG_HANDLE alg{};BCRYPT_HASH_HANDLE value{};
    if(BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)throw std::runtime_error("SHA256 initialization failed");
    struct Clean{BCRYPT_ALG_HANDLE& a;BCRYPT_HASH_HANDLE& h;~Clean(){if(h)BCryptDestroyHash(h);if(a)BCryptCloseAlgorithmProvider(a,0);}}clean{alg,value};
    if(BCryptCreateHash(alg,&value,nullptr,0,nullptr,0,0)<0)throw std::runtime_error("SHA256 creation failed");
    std::ifstream stream(file,std::ios::binary);if(!stream)throw std::runtime_error("Local foreground model is missing");std::array<char,65536> buffer{};
    while(stream){stream.read(buffer.data(),buffer.size());if(BCryptHashData(value,reinterpret_cast<PUCHAR>(buffer.data()),ULONG(stream.gcount()),0)<0)throw std::runtime_error("Model hash failed");}
    if(!stream.eof())throw std::runtime_error("Model read failed");std::array<UCHAR,32> digest{};if(BCryptFinishHash(value,digest.data(),ULONG(digest.size()),0)<0)throw std::runtime_error("Model hash finalization failed");
    std::string out;const char* hex="0123456789abcdef";for(auto c:digest){out+=hex[c>>4];out+=hex[c&15];}return out;
}
// The pinned RGB input recipe is implemented by the bounded antialiased sampler.
std::vector<float> preprocess(const RgbaImage& image,const ImportOptions& options){
    return subjectInputTensor(image,options);
}
}
struct OnnxSubjectProvider::Impl {
    Ort::Env environment{ORT_LOGGING_LEVEL_WARNING,"CompositorForeground"};Ort::SessionOptions options;Ort::Session session{nullptr};std::mutex mutex;
    explicit Impl(const std::filesystem::path& path){
        if(hash(path)!=OnnxSubjectProvider::modelSha256)throw std::runtime_error("Foreground model hash does not match pinned conversion");
        environment.DisableTelemetryEvents();options.SetIntraOpNumThreads(8);options.SetInterOpNumThreads(1);options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        // Do not retain the model's multi-gigabyte temporary arena after an edit.
        // This reduces retained memory; a hard inference-process budget remains a separate gate.
        options.DisableCpuMemArena();options.DisableMemPattern();
        // No accelerator/provider registration: ONNX Runtime's mandatory CPU provider is used.
        session=Ort::Session(environment,path.c_str(),options);
        if(session.GetInputCount()!=1||session.GetOutputCount()!=1)throw std::runtime_error("Unexpected model graph interface");
        auto inType=session.GetInputTypeInfo(0);auto outType=session.GetOutputTypeInfo(0);auto in=inType.GetTensorTypeAndShapeInfo(),out=outType.GetTensorTypeAndShapeInfo();
        if(in.GetElementType()!=ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT||out.GetElementType()!=ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT||in.GetShape()!=std::vector<int64_t>{1,3,1024,1024}||out.GetShape()!=std::vector<int64_t>{1,1,1024,1024})throw std::runtime_error("Unexpected model tensor format");
    }
};
OnnxSubjectProvider::OnnxSubjectProvider(const std::filesystem::path& path){initializeRuntime();impl_=std::make_unique<Impl>(path);}
OnnxSubjectProvider::~OnnxSubjectProvider()=default;
GrayMask OnnxSubjectProvider::infer(const RgbaImage& image,const ImportOptions& options){
    return inferImpl(image,options,true);
}
void OnnxSubjectProvider::healthCheck(const ImportOptions& options){
    RgbaImage image{2,2,8,std::vector<std::uint8_t>(16,255)};
    validate(inferImpl(image,options,false));
}
GrayMask OnnxSubjectProvider::inferImpl(const RgbaImage& image,const ImportOptions& options,bool requireSubject){
    auto data=preprocess(image,options);std::lock_guard lock(impl_->mutex);
    auto memory=Ort::MemoryInfo::CreateCpu(OrtArenaAllocator,OrtMemTypeDefault);const int64_t dims[]={1,3,1024,1024};auto tensor=Ort::Value::CreateTensor<float>(memory,data.data(),data.size(),dims,4);const char* names[]={"image"};const char* outputs[]={"mask"};Ort::RunOptions run;
    std::jthread cancellation([&](std::stop_token stop){while(!stop.stop_requested()){if(options.cancelled&&options.cancelled()){run.SetTerminate();return;}std::this_thread::sleep_for(std::chrono::milliseconds(20));}});
    auto result=impl_->session.Run(run,names,&tensor,1,outputs,1);cancellation.request_stop();checkCancelled(options);
    const auto outputInfo=result[0].GetTensorTypeAndShapeInfo();
    if(outputInfo.GetElementType()!=ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT||outputInfo.GetShape()!=std::vector<int64_t>{1,1,1024,1024})throw std::runtime_error("Foreground inference returned an unexpected output shape");
    const auto* mask=result[0].GetTensorData<float>();float max=0;for(std::size_t i=0;i<1024*1024;++i){if(!std::isfinite(mask[i]))throw std::runtime_error("Foreground inference returned non-finite mask");max=std::max(max,mask[i]);}if(requireSubject&&max<.5F)throw std::runtime_error("No foreground subject was detected");
    GrayMask out{image.width,image.height,image.width,std::vector<std::uint8_t>(std::size_t(image.width)*image.height)};
    for(std::uint32_t y=0;y<image.height;++y){checkCancelled(options);double sy=std::clamp((y+.5)*1024/image.height-.5,0.,1023.);int y0=int(sy),y1=std::min(y0+1,1023);float fy=float(sy-y0);for(std::uint32_t x=0;x<image.width;++x){double sx=std::clamp((x+.5)*1024/image.width-.5,0.,1023.);int x0=int(sx),x1=std::min(x0+1,1023);float fx=float(sx-x0);float value=(mask[y0*1024+x0]*(1-fx)+mask[y0*1024+x1]*fx)*(1-fy)+(mask[y1*1024+x0]*(1-fx)+mask[y1*1024+x1]*fx)*fy;out.pixels[std::size_t(y)*image.width+x]=std::uint8_t(std::clamp(value*255+.5F,0.F,255.F));}}return out;
}
struct OnnxObjectProvider::Impl {
    Ort::Env environment{ORT_LOGGING_LEVEL_WARNING,"CompositorObject"};
    Ort::SessionOptions options;Ort::Session encoder{nullptr},decoder{nullptr};std::mutex mutex;
    explicit Impl(const std::filesystem::path& directory){
        auto vision=directory/L"slimsam-vision.onnx",prompt=directory/L"slimsam-decoder.onnx";
        if(hash(vision)!="9f8433273a6750b587779baa0cf5508111001bf7e7acfcf585d370139fd366d0"||hash(prompt)!="f4514391764fbd56e08e119060d874ecd7d52994bfb1968af159e12d4943b5bb")throw std::runtime_error("Object selection model does not match its pinned version");
        environment.DisableTelemetryEvents();options.SetIntraOpNumThreads(8);options.SetInterOpNumThreads(1);options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);options.DisableCpuMemArena();options.DisableMemPattern();
        encoder=Ort::Session(environment,vision.c_str(),options);decoder=Ort::Session(environment,prompt.c_str(),options);
        if(encoder.GetInputCount()!=1||encoder.GetOutputCount()!=2||decoder.GetInputCount()!=4||decoder.GetOutputCount()!=2)throw std::runtime_error("Unexpected object selection model interface");
    }
};
OnnxObjectProvider::OnnxObjectProvider(const std::filesystem::path& directory){initializeRuntime();impl_=std::make_unique<Impl>(directory);}
OnnxObjectProvider::~OnnxObjectProvider()=default;
GrayMask OnnxObjectProvider::infer(const RgbaImage& image,double x,double y,const ImportOptions& options){
    validate(image);checkCancelled(options);if(!std::isfinite(x)||!std::isfinite(y)||x<0||y<0||x>=image.width||y>=image.height)throw std::invalid_argument("Object selection point is outside the image");
    std::lock_guard lock(impl_->mutex);
    const double scale=1024./std::max(image.width,image.height);const int width=std::max(1,int(std::floor(image.width*scale+.5))),height=std::max(1,int(std::floor(image.height*scale+.5)));
    QImage source(image.pixels.data(),int(image.width),int(image.height),qsizetype(image.stride),QImage::Format_RGBA8888_Premultiplied);
    auto resized=source.convertToFormat(QImage::Format_RGB888).scaled(width,height,Qt::IgnoreAspectRatio,Qt::SmoothTransformation);
    std::vector<float> input(3*1024*1024);constexpr float mean[]{.485f,.456f,.406f},deviation[]{.229f,.224f,.225f};
    for(int py=0;py<height;++py){checkCancelled(options);const auto*row=resized.constScanLine(py);for(int px=0;px<width;++px)for(int c=0;c<3;++c)input[size_t(c)*1024*1024+size_t(py)*1024+px]=(row[px*3+c]/255.f-mean[c])/deviation[c];}
    auto memory=Ort::MemoryInfo::CreateCpu(OrtArenaAllocator,OrtMemTypeDefault);std::array<int64_t,4> dimensions{1,3,1024,1024};auto tensor=Ort::Value::CreateTensor<float>(memory,input.data(),input.size(),dimensions.data(),dimensions.size());
    Ort::RunOptions run;std::jthread cancellation([&](std::stop_token stop){while(!stop.stop_requested()){if(options.cancelled&&options.cancelled()){run.SetTerminate();return;}std::this_thread::sleep_for(std::chrono::milliseconds(20));}});
    const char*encoderNames[]{"pixel_values"};const char*embeddingNames[]{"image_embeddings","image_positional_embeddings"};auto embeddings=impl_->encoder.Run(run,encoderNames,&tensor,1,embeddingNames,2);checkCancelled(options);
    std::array<float,4> points{float(x*width/image.width),float(y*height/image.height),0,0};std::array<int64_t,2> labels{1,-1};std::array<int64_t,4> pointShape{1,1,2,2};std::array<int64_t,3> labelShape{1,1,2};
    std::vector<Ort::Value> values;values.push_back(Ort::Value::CreateTensor<float>(memory,points.data(),points.size(),pointShape.data(),pointShape.size()));values.push_back(Ort::Value::CreateTensor<int64_t>(memory,labels.data(),labels.size(),labelShape.data(),labelShape.size()));values.push_back(std::move(embeddings[0]));values.push_back(std::move(embeddings[1]));
    const char*decoderNames[]{"input_points","input_labels","image_embeddings","image_positional_embeddings"};const char*outputs[]{"iou_scores","pred_masks"};auto result=impl_->decoder.Run(run,decoderNames,values.data(),values.size(),outputs,2);checkCancelled(options);
    if(result[0].GetTensorTypeAndShapeInfo().GetElementCount()!=3||result[1].GetTensorTypeAndShapeInfo().GetElementCount()!=3*256*256)throw std::runtime_error("Unexpected object mask dimensions");
    const float*scores=result[0].GetTensorData<float>(),*masks=result[1].GetTensorData<float>();int chosen=0;for(int i=0;i<3;++i){if(!std::isfinite(scores[i]))throw std::runtime_error("Object model produced an invalid score");if(scores[i]>scores[chosen])chosen=i;}
    // SAM returns object/part alternatives. Among similarly confident proposals,
    // prefer the complete clicked instance over a label, handle or other small part.
    const float bestScore=scores[chosen];size_t largest=0;const int seedX=std::clamp(int(x*width/image.width/4),0,255),seedY=std::clamp(int(y*height/image.height/4),0,255);
    for(int i=0;i<3;++i){const auto*candidate=masks+i*256*256;if(scores[i]<bestScore-.04f||candidate[seedY*256+seedX]<=0)continue;size_t area=0;for(int py=0;py<(height+3)/4;++py)for(int px=0;px<(width+3)/4;++px)area+=candidate[py*256+px]>0;if(area>largest&&area<size_t(width)*height/16*.95){largest=area;chosen=i;}}
    const auto*mask=masks+chosen*256*256;
    auto sample=[&](double px,double py){px=std::clamp(px,0.,255.);py=std::clamp(py,0.,255.);const int ix=int(px),iy=int(py);const double fx=px-ix,fy=py-iy;return (1-fy)*((1-fx)*mask[iy*256+ix]+fx*mask[iy*256+std::min(255,ix+1)])+fy*((1-fx)*mask[std::min(255,iy+1)*256+ix]+fx*mask[std::min(255,iy+1)*256+std::min(255,ix+1)]);};
    GrayMask out{image.width,image.height,image.width,std::vector<uint8_t>(checkedBytes(image.width,image.height,1,options))};
    for(uint32_t py=0;py<image.height;++py){checkCancelled(options);for(uint32_t px=0;px<image.width;++px){double value=sample((px+.5)*width/image.width/4-.5,(py+.5)*height/image.height/4-.5);if(!std::isfinite(value))throw std::runtime_error("Object model produced invalid mask pixels");out.pixels[size_t(py)*out.width+px]=value>0?255:0;}}
    return out;
}

}
