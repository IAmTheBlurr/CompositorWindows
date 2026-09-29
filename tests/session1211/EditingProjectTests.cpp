#include "core/DocumentLimits.h"
#include "editing/DocumentGeometry.h"
#include "editing/Text.h"
#include "effects/Adjustments.h"
#include "persistence/ProjectStore.h"
#include "imaging/onnx_subject_provider.h"
#include "imaging/wic_codec.h"
#include "ui/MainWindow.h"
#include "ui/CommandRegistry.h"
#include <QApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTemporaryDir>
#include <QSettings>
#include <QTest>
#include <QTimer>
#include <QDialog>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QTableWidget>
#include <QKeySequenceEdit>
#include <iostream>
#include <stdexcept>
#include <filesystem>

using namespace compositor;
#define REQUIRE(...) do{if(!(__VA_ARGS__))throw std::runtime_error(std::string("line ")+std::to_string(__LINE__)+": " #__VA_ARGS__);}while(false)
namespace {
template<class F>bool rejected(F f){try{f();return false;}catch(const std::exception&){return true;}}
Document fixture(){Document d;d.id=newId();d.width=96;d.height=64;Layer folder;folder.id=newId();folder.name="Folder";folder.group=true;folder.opacity=.6;folder.transform={0,0,96,64};Layer layer;layer.id=newId();layer.name="Pixels";layer.parentId=folder.id;layer.transform={20,12,40,32};layer.raster=Raster::filled(40,32,{150,90,50,255});d.layers={folder,layer};d.guides={{newId(),CanvasGuide::Axis::Vertical,30},{newId(),CanvasGuide::Axis::Horizontal,22}};return d;}
QJsonObject manifest(const QString&path){QFile f(path+"/manifest.json");REQUIRE(f.open(QIODevice::ReadOnly));return QJsonDocument::fromJson(f.readAll()).object();}
void writeManifest(const QString&path,QJsonObject value){QFile f(path+"/manifest.json");REQUIRE(f.open(QIODevice::WriteOnly));f.write(QJsonDocument(value).toJson());}
void format(){
    QTemporaryDir temp;const auto path=temp.path()+"/full.comp";ProjectStore store(makeWicProjectCodec());auto d=fixture();editing::TextStyle style;style.content="Format 9";style.fontSize=18;auto text=editing::createTextLayer({4,3},style);text.parentId=d.layers[0].id;text.effectsJson=R"({"stroke":{"size":2,"red":0.1,"green":0.5,"blue":0.8,"opacity":0.9,"inside":false,"enabled":false}})";d.layers.push_back(text);Layer adjustment;adjustment.id=newId();adjustment.name="Noise";adjustment.transform={0,0,96,64};adjustment.adjustmentJson=effects::defaultAdjustmentJson("Add Noise");d.layers.push_back(adjustment);
    store.save(path.toStdWString(),d,text.id);auto before=SoftwareRenderer().render(d,0,0,d.width,d.height)->rgba();auto opened=store.load(path.toStdWString());REQUIRE(opened.readVersion==9);REQUIRE(opened.document.guides==d.guides);REQUIRE(opened.document.layers[2].textJson==d.layers[2].textJson);REQUIRE(QJsonDocument::fromJson(QByteArray::fromStdString(opened.document.layers[2].effectsJson))==QJsonDocument::fromJson(QByteArray::fromStdString(d.layers[2].effectsJson)));REQUIRE(SoftwareRenderer().render(opened.document,0,0,d.width,d.height)->rgba()==before);
    auto original=manifest(path);for(int version:{1,7,8}){auto altered=original;altered["version"]=version;writeManifest(path,altered);REQUIRE(rejected([&]{store.load(path.toStdWString());}));}writeManifest(path,original);
    auto altered=original;auto layers=altered["layers"].toArray();auto record=layers[2].toObject();auto metadata=record["text"].toObject();metadata["colorRuns"]=QJsonArray{};record["text"]=metadata;layers[2]=record;altered["layers"]=layers;writeManifest(path,altered);REQUIRE(rejected([&]{store.load(path.toStdWString());}));writeManifest(path,original);
    auto changed=opened.document;changed.layers[1].raster=Raster::filled(40,32,{0,0,0,255});store.save(path.toStdWString(),changed,text.id);REQUIRE(opened.document.layers[1].raster->pixel(0,0)==Pixel{150,90,50,255});
    ProjectStore failing(makeWicProjectCodec(),[](SaveFaultPoint p){if(p==SaveFaultPoint::AfterStage)throw std::runtime_error("injected");});REQUIRE(rejected([&]{failing.save(path.toStdWString(),d,text.id);}));REQUIRE(store.load(path.toStdWString()).document.layers[1].raster->pixel(0,0)==Pixel{0,0,0,255});
    // Earlier supported metadata remains readable when newer semantics are absent.
    d.layers.resize(2);d.layers[0].opacity=1;d.guides.clear();store.save(path.toStdWString(),d,d.layers[1].id);auto base=manifest(path);for(int version=2;version<=9;++version){base["version"]=version;writeManifest(path,base);REQUIRE(store.load(path.toStdWString()).readVersion==version);}
}
void history(){std::optional<Document>d=fixture();History h;h.begin("first",d,"");d->layers[1].opacity=.8;h.end(d,"");const auto captured=h.currentRevision();const auto snapshot=*d;h.begin("later",d,"");d->layers[1].opacity=.4;h.end(d,"");h.markSaved(captured);REQUIRE(h.modified());REQUIRE(snapshot.layers[1].opacity==.8);auto undo=h.undo();REQUIRE(undo);REQUIRE(!h.modified());h.redo();REQUIRE(h.modified());}
void geometry(){auto d=fixture();auto crop=editing::cropDocument(d,{10,5,80,50});REQUIRE(crop.guides[0].position==20&&crop.guides[1].position==17);auto resized=editing::imageResize(d,{192,128,72});REQUIRE(resized.guides[0].position==60&&resized.guides[1].position==44);auto flipped=editing::flipCanvas(d,true);REQUIRE(flipped.guides[0].position==66&&flipped.guides[1].position==22);auto bounds=editing::trimBounds(d,{});REQUIRE(bounds==editing::Rect{20,12,40,32});editing::TrimOptions sides;sides.left=false;REQUIRE(editing::trimBounds(d,sides)==editing::Rect{0,12,60,32});REQUIRE(!editing::trimBounds(d,{},[]{return true;}));d.guides.push_back(d.guides.front());REQUIRE(rejected([&]{validateDocument(d);}));}
void feather(){auto smallFeather=editing::rasterSelection(editing::SelectionOutline::rectangle({40,40,40,40}),128,128,1);REQUIRE(smallFeather->coverage->pixel(39,60)>0);auto outline=editing::SelectionOutline::rectangle({40,40,40,40});auto selection=editing::rasterSelection(outline,128,128,12);REQUIRE(selection->coverage->pixel(35,60)>0);REQUIRE(selection->coverage->pixel(40,60)<200);REQUIRE(selection->coverage->pixel(60,60)>250);REQUIRE(std::abs(int(selection->coverage->pixel(39,60))+selection->coverage->pixel(40,60)-255)<=2);auto moved=editing::moveSelectionCoverage(selection,{7,8});REQUIRE(moved->feather==12);REQUIRE(moved->coverage->pixel(47,68)==selection->coverage->pixel(40,60));auto big=editing::rasterSelection(editing::SelectionOutline::rectangle({250,0,30,600}),600,600,30);REQUIRE(std::abs(int(big->coverage->pixel(255,300))-big->coverage->pixel(256,300))<10);REQUIRE(editing::selectionMode(true,true)==editing::SelectionMode::Subtract);}
void large(){Document d;d.id=newId();d.width=11000;d.height=5300;Layer layer;layer.id=newId();layer.name="Large source";layer.transform={0,0,11000,5300};layer.raster=Raster::filled(11000,5300,{20,40,60,255});d.layers.push_back(layer);for(int i=0;i<2;++i){auto copy=layer;copy.id=newId();copy.opacity=.5;d.layers.push_back(copy);}validateDocument(d);const auto flattened=Raster::materializationCount();CompositeCache cache;auto viewport=cache.renderViewport(d,4900,2300,640,480,1,64,128);REQUIRE(viewport.raster&&viewport.raster->pixel(20,20).a==255);REQUIRE(Raster::materializationCount()==flattened);REQUIRE(cache.viewportRetainedTiles()<=128);}
void objects(const char*root){const auto source=std::filesystem::path(root);auto image=imaging::WicCodec::decode(source/"tests/session1211/objects-scene.png").image;imaging::OnnxObjectProvider provider(source/"dependencies/imaging/model");auto cup=provider.infer(image,199,219),bottle=provider.infer(image,451,200);auto pixel=[](const imaging::GrayMask&m,int x,int y){return m.pixels[size_t(y)*m.stride+x];};REQUIRE(pixel(cup,199,219)==255&&pixel(cup,451,200)==0);REQUIRE(pixel(bottle,451,200)==255&&pixel(bottle,199,219)==0);REQUIRE(pixel(bottle,451,290)==255);size_t intersection=0,ca=0,ba=0;for(size_t i=0;i<cup.pixels.size();++i){ca+=cup.pixels[i]>0;ba+=bottle.pixels[i]>0;intersection+=cup.pixels[i]>0&&bottle.pixels[i]>0;}REQUIRE(ca>20000&&ba>18000&&intersection<500);auto touching=imaging::WicCodec::decode(source/"tests/session1211/touching-objects.png").image;const std::array<Point,3> prompts{{{186,263},{351,219},{519,288}}};for(size_t i=0;i<prompts.size();++i){auto instance=provider.infer(touching,prompts[i].x,prompts[i].y);for(size_t j=0;j<prompts.size();++j)REQUIRE(pixel(instance,int(prompts[j].x),int(prompts[j].y))==(i==j?255:0));}
    imaging::ImportOptions cancel;cancel.cancelled=[]{return true;};REQUIRE(rejected([&]{provider.infer(image,199,219,cancel);}));}
QAction*command(MainWindow&w,const char*id){for(auto*a:w.findChildren<QAction*>())if(a->property("commandId")==id)return a;throw std::runtime_error(std::string("Missing ")+id);}
void controls(){MainWindow w(true);auto&p=w.addProject(fixture());w.show();QApplication::processEvents();command(w,"view.rulers")->trigger();REQUIRE(w.canvas()->showRulers);command(w,"view.grid")->trigger();REQUIRE(w.canvas()->showLayoutGrid);command(w,"view.guides")->trigger();REQUIRE(!w.canvas()->showGuides);auto before=p.document;command(w,"view.clearGuides")->trigger();REQUIRE(p.document->guides.empty());command(w,"edit.undo")->trigger();REQUIRE(p.document==before);
    QTimer::singleShot(100,[&]{auto*d=w.findChild<QDialog*>("keyboardShortcutsDialog");REQUIRE(d&&d->isVisible());auto*table=d->findChild<QTableWidget*>();REQUIRE(table&&table->rowCount()>60);for(int row=0;row<table->rowCount();++row)if(table->item(row,0)->text().contains("Move (V)")){auto*edit=qobject_cast<QKeySequenceEdit*>(table->cellWidget(row,1));REQUIRE(edit);edit->setKeySequence(QKeySequence("F6"));}d->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Save)->click();});command(w,"edit.shortcuts")->trigger();REQUIRE(command(w,"tool.move")->shortcut()==QKeySequence("F6"));}
}
int main(int argc,char**argv){QApplication app(argc,argv);QTemporaryDir settings;QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settings.path());QCoreApplication::setOrganizationName("CompositorTests");QCoreApplication::setApplicationName("Session1211");try{REQUIRE(argc>=2);const std::string name=argv[1];if(name=="format")format();else if(name=="history")history();else if(name=="geometry")geometry();else if(name=="feather")feather();else if(name=="large")large();else if(name=="objects"){REQUIRE(argc==3);objects(argv[2]);}else if(name=="controls")controls();else throw std::runtime_error("Unknown case");std::cout<<"PASS "<<name<<'\n';return 0;}catch(const std::exception&e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
