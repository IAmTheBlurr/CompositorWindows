#include "MainWindow.h"
#include "PropertyControls.h"
#include <QMenuBar>
#include <QSettings>
#include <QDialog>
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QInputDialog>
#include <QMessageBox>
#include <QApplication>
#include <QProgressDialog>
#include <QFutureWatcher>
#include <QSpinBox>
#include <atomic>
#include <QtConcurrent>
#include <cmath>

namespace compositor {
namespace {
bool preference(const char* name,bool fallback){return QSettings().value(QString("tool/")+name,fallback).toBool();}
}
void MainWindow::setupLayoutActions(){
    QMenu* view=nullptr;QMenu* image=nullptr;
    for(auto* menu:menuBar()->findChildren<QMenu*>()){if(menu->title().remove('&')=="View")view=menu;if(menu->title().remove('&')=="Image")image=menu;}
    for(auto*menu:menuBar()->findChildren<QMenu*>())if(menu->title().remove('&')=="Edit")action(menu,"Keyboard Shortcuts…",{},[this]{commands_->showShortcutEditor();});
    if(!view)return;
    action(view,"Zoom In",QKeySequence("Ctrl+="),[this]{if(canvas())canvas()->zoomAt(canvas()->zoom*1.25,canvas()->rect().center());});
    action(view,"Zoom Out",QKeySequence("Ctrl+-"),[this]{if(canvas())canvas()->zoomAt(canvas()->zoom/1.25,canvas()->rect().center());});
    auto toggle=[&](QMenu* menu,const char* label,const char* key,bool fallback,const char* shortcut=""){
        auto* item=action(menu,label,QKeySequence(shortcut),[this,key]{QSettings settings;settings.setValue(QString("tool/")+key,!preference(key,std::string(key)!="rulers"&&std::string(key)!="grid"&&std::string(key)!="lockGuides"));refreshLayout();});
        item->setCheckable(true);item->setProperty("layoutPreference",key);item->setChecked(preference(key,fallback));return item;
    };
    toggle(view,"Rulers","rulers",false,"Ctrl+R");toggle(view,"Guides","guides",true,"Ctrl+;");toggle(view,"Grid","grid",false,"Ctrl+'");toggle(view,"Lock Guides","lockGuides",false);
    action(view,"New Guide…",{},[this]{if(!canEditLayers())return;QDialog dialog(this);dialog.setWindowTitle("New Guide");QFormLayout layout(&dialog);QComboBox axis;axis.addItems({"Horizontal","Vertical"});ui::PropertyNumber position;position.setRange(-1000000,1000000);position.setDecimals(2);position.setSuffix(" px");layout.addRow("Orientation",&axis);layout.addRow("Position",&position);QDialogButtonBox buttons(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);layout.addRow(&buttons);connect(&buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);connect(&buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);if(dialog.exec()!=QDialog::Accepted)return;QSettings().setValue("tool/guides",true);edit("New Guide",[&](Document&d){d.guides.push_back({newId(),axis.currentIndex()?CanvasGuide::Axis::Vertical:CanvasGuide::Axis::Horizontal,position.value()});});});
    action(view,"Clear Guides",{},[this]{if(canEditLayers())edit("Clear Guides",[](Document&d){d.guides.clear();});});
    toggle(view,"Snap","snap",true);auto* snap=view->addMenu("Snap To");toggle(snap,"Guides","snapGuides",true);toggle(snap,"Grid","snapGrid",true);toggle(snap,"Layers","snapLayers",true);toggle(snap,"Document Bounds","snapBounds",true);
    if(image)action(image,"Trim…",{},[this]{trimImage();});
}
void MainWindow::refreshLayout(){
    snapping_=preference("snap",true);
    for(auto* item:findChildren<QAction*>())if(item->property("layoutPreference").isValid()){auto key=item->property("layoutPreference").toString();item->setChecked(preference(key.toUtf8().constData(),key!="rulers"&&key!="grid"&&key!="lockGuides"));}
    for(auto& project:projects_){auto* c=project->canvas;if(!c)continue;c->showRulers=preference("rulers",false);c->showGuides=preference("guides",true);c->showLayoutGrid=preference("grid",false);c->lockGuides=preference("lockGuides",false);c->guides=project->document?project->document->guides:std::vector<CanvasGuide>{};
        auto* owner=project.get();c->canEditGuide=[this,owner]{return current()==owner&&canEditLayers();};
        c->guideCommitted=[this,owner](CanvasGuide guide,bool remove){if(current()!=owner||!canEditLayers())return;edit(remove?"Delete Guide":"Place Guide",[&](Document&d){auto found=std::find_if(d.guides.begin(),d.guides.end(),[&](auto&g){return g.id==guide.id;});if(found!=d.guides.end()){if(remove)d.guides.erase(found);else *found=guide;}else if(!remove)d.guides.push_back(guide);});};
        c->guideSnap=[this](double value,CanvasGuide::Axis axis,const std::string&id){if(!preference("snap",true)||!snapping_||!current()||!current()->document)return value;auto targets=alignmentTargets();auto& values=axis==CanvasGuide::Axis::Vertical?targets.xs:targets.ys;for(const auto&g:current()->document->guides)if(g.id==id)values.erase(std::remove(values.begin(),values.end(),g.position),values.end());double distance=8/std::max(.0001,canvas()->pointsPerPixel()),best=value;for(double target:values)if(std::abs(target-value)<=distance){distance=std::abs(target-value);best=target;}return best;};c->update();
    }
}
editing_transform::SnapTargets MainWindow::alignmentTargets(std::span<const std::string> excluded,bool centers){
    editing_transform::SnapTargets result;auto* p=current();if(!p||!p->document||!preference("snap",true)||!snapping_)return result;const auto&d=*p->document;
    if(preference("snapBounds",true)){result.xs={0,double(d.width)};result.ys={0,double(d.height)};if(centers){result.xs.push_back(d.width/2.);result.ys.push_back(d.height/2.);}}
    if(preference("snapLayers",true))for(const auto&placement:editing_transform::visiblePlacements(d))if(std::find(excluded.begin(),excluded.end(),placement.id)==excluded.end()){const auto b=editing_transform::bounds(placement.transform);result.xs.insert(result.xs.end(),{std::round(b.x),std::round(b.x+b.width)});result.ys.insert(result.ys.end(),{std::round(b.y),std::round(b.y+b.height)});if(centers){result.xs.push_back(std::round(b.x+b.width/2));result.ys.push_back(std::round(b.y+b.height/2));}}
    if(preference("snapGuides",true)&&preference("guides",true))for(const auto&g:d.guides)(g.axis==CanvasGuide::Axis::Vertical?result.xs:result.ys).push_back(g.position);
    if(preference("snapGrid",true)&&preference("grid",false)){for(int x=0;x<=d.width;x+=8)result.xs.push_back(x);for(int y=0;y<=d.height;y+=8)result.ys.push_back(y);}return result;
}
void MainWindow::trimImage(){
    auto* p=current();if(!p||!p->document||!canEditLayers())return;
    QDialog dialog(this);dialog.setObjectName("trimDialog");dialog.setWindowTitle("Trim");QFormLayout layout(&dialog);QComboBox based;based.addItems({"Transparent Pixels","Top Left Pixel Color","Bottom Right Pixel Color"});layout.addRow("Based on",&based);std::array<QCheckBox*,4> sides{};int i=0;for(const auto*name:{"Top","Bottom","Left","Right"}){sides[i]=new QCheckBox(name,&dialog);sides[i]->setChecked(true);layout.addRow(sides[i++]);}QSpinBox tolerance;tolerance.setRange(0,255);layout.addRow("Color tolerance",&tolerance);QDialogButtonBox buttons(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);layout.addRow(&buttons);connect(&buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);connect(&buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);if(dialog.exec()!=QDialog::Accepted)return;
    editing::TrimOptions options;options.basedOn=editing::TrimBasis(based.currentIndex());options.tolerance=tolerance.value();options.top=sides[0]->isChecked();options.bottom=sides[1]->isChecked();options.left=sides[2]->isChecked();options.right=sides[3]->isChecked();
    const auto snapshot=*p->document;auto cancelled=std::make_shared<std::atomic_bool>(false);p->projectBusy=true;refresh(false,false);
    QProgressDialog progress("Finding image bounds…","Cancel",0,0,this);progress.setWindowModality(Qt::WindowModal);progress.setMinimumDuration(100);
    QFutureWatcher<std::optional<editing::Rect>> watcher;QEventLoop loop;connect(&watcher,&QFutureWatcherBase::finished,&loop,&QEventLoop::quit);connect(&progress,&QProgressDialog::canceled,this,[cancelled]{*cancelled=true;});
    watcher.setFuture(QtConcurrent::run([snapshot,options,cancelled]{return editing::trimBounds(snapshot,options,[cancelled]{return cancelled->load();});}));loop.exec();progress.close();p->projectBusy=false;refresh(false,false);
    const auto bounds=watcher.result();if(cancelled->load())return;if(!bounds){QMessageBox::information(this,"Trim","No content remained after trimming.");return;}edit("Trim",[&](Document&d){d=editing::cropDocument(d,*bounds);});
}
}
