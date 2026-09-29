#include "MainWindow.h"
#include "imaging/onnx_subject_provider.h"
#include <QApplication>
#include <QDir>
#include <QFutureWatcher>
#include <QProgressDialog>
#include <QtConcurrent>
#include <atomic>
#include <cmath>

namespace compositor {
void MainWindow::selectSubjectOrObject(std::optional<Point> point,Qt::KeyboardModifiers modifiers){
    auto*p=current();if(!p||!p->document||!canEditLayers())return;
    if(point&&(point->x<0||point->y<0||point->x>=p->document->width||point->y>=p->document->height))return;
    auto sample=*p->document;const auto before=sample;sample.selection.reset();
    if(!wandAllLayers_){sample.layers.clear();if(const auto*l=active();l&&!l->group&&l->raster){auto layer=*l;layer.mask.reset();layer.maskSourceId.clear();layer.parentId.clear();layer.visible=true;layer.opacity=1;layer.blend=Blend::Normal;sample.layers.push_back(std::move(layer));}}
    if(sample.layers.empty())return;
    const auto mode=editing::selectionMode(modifiers.testFlag(Qt::ShiftModifier),modifiers.testFlag(Qt::AltModifier),selectionMode_);const bool antialiased=selectionAntialias_;const int edge=objectEdgeOffset_;
    auto model=std::filesystem::path(QDir(QApplication::applicationDirPath()).filePath("models").toStdWString());
    if(!std::filesystem::exists(model/L"birefnet-lite.onnx"))model=std::filesystem::path(COMPOSITOR_SOURCE_ROOT)/"dependencies/imaging/model";
    auto cancel=std::make_shared<std::atomic_bool>(false);struct Result{std::optional<editing::SelectionOutline> outline;std::string error;};
    p->projectBusy=true;refresh(false,false);QProgressDialog progress(point?"Selecting object…":"Selecting subject…","Cancel",0,0,this);progress.setObjectName("objectSelectionProgress");progress.setWindowModality(Qt::WindowModal);progress.setMinimumDuration(100);progress.setAutoClose(false);
    connect(&progress,&QProgressDialog::canceled,this,[cancel]{*cancel=true;});QFutureWatcher<Result> watcher;connect(&watcher,&QFutureWatcher<Result>::finished,&progress,&QProgressDialog::accept);
    auto future=QtConcurrent::run([sample,point,antialiased,model,cancel,edge]{Result result;try{
        const double units=std::max(1.,std::max(sample.width,sample.height)/2048.);const int width=int(std::ceil(sample.width/units)),height=int(std::ceil(sample.height/units));
        auto rendered=SoftwareRenderer().renderScaled(sample,0,0,width,height,units);imaging::RgbaImage image{uint32_t(width),uint32_t(height),size_t(width)*4,rendered->rgba()};imaging::ImportOptions limits;limits.cancelled=[cancel]{return cancel->load();};
        imaging::GrayMask mask;
        if(point){imaging::OnnxObjectProvider provider(model);mask=provider.infer(image,point->x/units,point->y/units,limits);}else{imaging::OnnxSubjectProvider provider(model/L"birefnet-lite.onnx");mask=provider.infer(image,limits);for(auto&v:mask.pixels)v=v>=128?255:0;}
        imaging::checkCancelled(limits);GrayRaster gray{width,height,std::move(mask.pixels)};
        if(gray.hasCoverage()){auto outline=editing::SelectionOutline::fromCoverage(gray,antialiased);result.outline=outline.affineMapped({units,0,0,units,0,0}).clipped(sample.width,sample.height);if(point&&edge)result.outline=result.outline->resized(-edge,sample.width,sample.height);}
    }catch(const std::exception&error){result.error=error.what();}return result;});watcher.setFuture(future);progress.exec();if(progress.wasCanceled())*cancel=true;future.waitForFinished();p->projectBusy=false;refresh(false,false);
    if(cancel->load()||current()!=p||!p->document||*p->document!=before)return;auto result=future.result();if(!result.error.empty())throw std::runtime_error(result.error);
    edit(point?"Object Selection":"Select Subject",[&](Document&d){if(!result.outline){if(mode==editing::SelectionMode::Replace)d.selection.reset();return;}std::optional<editing::SelectionOutline> previous;if(d.selection){if(d.selection->outline)previous=*d.selection->outline;else if(d.selection->coverage)previous=editing::SelectionOutline::fromCoverage(*d.selection->coverage);}auto combined=editing::applySelection(previous,*result.outline,mode,d.width,d.height,antialiased);d.selection=editing::rasterSelection(combined,d.width,d.height);});
}
}
