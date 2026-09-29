#include "MainWindow.h"
#include "LayerClipboard.h"
#include "ProjectLayerCopyJob.h"
#include "LayerCopyCommit.h"
#include <QApplication>
#include <QBuffer>
#include <QClipboard>
#include <QStatusBar>
#include <algorithm>

namespace compositor {
bool MainWindow::copyWholeLayers(){
    auto* project=current();if(!project||!project->document||project->document->selection||project->maskSelected||!canEditLayers())return false;
    auto ids=layers::copiedLayerRoots(*project->document,layerSelection());if(ids.empty())return false;
    auto snapshot=std::make_shared<const ui::CopiedLayers>(ui::CopiedLayers{*project->document,std::move(ids),project->canvas});
    auto mime=std::make_unique<ui::CopiedLayerMimeData>(std::move(snapshot));
    // Keep ordinary PNG clipboard interoperability for image-bearing layers.
    if(active()&&active()->raster&&!active()->group)if(auto copied=editing::copyPixels(*project->document,project->active,SoftwareRenderer())){
        const auto bytes=copied->raster->rgba();QImage image(bytes.data(),copied->raster->width,copied->raster->height,copied->raster->width*4,QImage::Format_RGBA8888_Premultiplied);mime->setImageData(image.copy());QByteArray png;QBuffer buffer(&png);buffer.open(QIODevice::WriteOnly);if(!image.save(&buffer,"PNG"))throw std::runtime_error("Unable to copy layer pixels");mime->setData("image/png",png);
    }
    QApplication::clipboard()->setMimeData(mime.release());return true;
}
bool MainWindow::pasteWholeLayers(){
    auto* target=current();if(!target||(target->document?!canEditLayers():!canSwitchProjects()))return false;
    const auto copied=ui::copiedLayerSnapshot(QApplication::clipboard()->mimeData());if(!copied||copied->roots.empty())return false;
    if(ui::ProjectLayerCopyJob::find(this))return false;
    QPointer<QObject> token=target->canvas;const bool sameProject=copied->origin==token;
    auto project=[this,token]()->EditorProject*{for(auto& item:projects_)if(item->canvas==token)return item.get();return nullptr;};
    ui::ProjectLayerCopyJob::Host host;
    host.prepare=[project,copied,sameProject](const std::string& id)->std::optional<ui::LayerCopyWork>{
        auto* owner=project();if(!owner)return {};ui::LayerCopyWork work;work.source=copied->document;work.root=id;work.roots=copied->roots;work.target=owner->canvas;work.before={owner->document,owner->active};
        if(owner->document)work.destination=*owner->document;else{work.destination.id=newId();work.destination.width=copied->document.width;work.destination.height=copied->document.height;work.destination.resolution=copied->document.resolution;}
        if(sameProject){
            // copyLayers centers a batch by default. Supplying its original
            // center keeps the captured transforms exactly in the source tab.
            std::unordered_set<std::string> included(work.roots.begin(),work.roots.end());for(const auto& root:work.roots){auto children=layers::descendants(work.source,root);included.insert(children.begin(),children.end());}
            const auto root=std::find_if(work.source.layers.begin(),work.source.layers.end(),[&](const Layer& l){return l.id==work.roots.front();});if(root==work.source.layers.end())throw std::runtime_error("Invalid copied layer snapshot");work.point=root->transform.fromUnit({.5,.5});
            if(work.roots.size()>1){double left=1e20,top=1e20,right=-1e20,bottom=-1e20;for(const auto& layer:work.source.layers)if(included.contains(layer.id)&&!layer.group){left=std::min(left,layer.transform.x);top=std::min(top,layer.transform.y);right=std::max(right,layer.transform.x+layer.transform.width);bottom=std::max(bottom,layer.transform.y+layer.transform.height);}if(left<right&&top<bottom)work.point=Point{(left+right)/2,(top+bottom)/2};}
        }
        return work;
    };
    host.commit=[this,project,sameProject](const ui::LayerCopyWork& work,layers::CopyResult copy){
        auto* owner=project();if(!owner)return;copy.edit.action="Paste";
        if(sameProject){
            std::string anchor=work.before.active;for(const auto& entry:layers::entries(work.destination,true))if(std::find(work.roots.begin(),work.roots.end(),entry.id)!=work.roots.end()){anchor=entry.id;break;}
            const auto at=std::find_if(work.destination.layers.begin(),work.destination.layers.end(),[&](const Layer& l){return l.id==anchor;});const auto count=work.destination.layers.size();const size_t insertion=at==work.destination.layers.end()?count:size_t(at-work.destination.layers.begin())+1;
            const auto parent=at==work.destination.layers.end()?std::string{}:at->parentId;
            for(auto& layer:copy.edit.document.layers)if(std::find(copy.edit.selection.ids.begin(),copy.edit.selection.ids.end(),layer.id)!=copy.edit.selection.ids.end()){layer.parentId=parent;layer.name+=" copy";}
            if(insertion<count)std::rotate(copy.edit.document.layers.begin()+insertion,copy.edit.document.layers.begin()+count,copy.edit.document.layers.end());
            validateDocument(copy.edit.document);
        }
        const bool first=ui::commitPreparedLayerCopy(*owner,work.before.document,work.before.active,copy.edit);owner->composite.reset();refresh();if(first)owner->canvas->fit();
    };
    host.settled=[project](const ui::LayerCopyWork&){if(auto* owner=project())owner->projectBusy=false;};
    host.error=[this](const QString& error){statusBar()->showMessage("Paste: "+error);};
    host.finished=[this,project]{if(auto* owner=project())owner->projectBusy=false;managingProjectOpen_=false;refresh();};
    auto* job=new ui::ProjectLayerCopyJob({copied->roots.front()},std::move(host),this);target->projectBusy=true;managingProjectOpen_=true;
    try{refresh(false,false);}catch(...){target->projectBusy=false;managingProjectOpen_=false;delete job;throw;}return true;
}
}
