#include "ui/MainWindow.h"
#include "ui/VisualStyle.h"
#include <QApplication>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QPushButton>
#include <QTabWidget>
#include <QTest>
#include <iostream>
#include <stdexcept>
using namespace compositor;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class F>void wait(F f){QElapsedTimer timer;timer.start();while(!f()){if(timer.elapsed()>30000)throw std::runtime_error("Editor preview timeout");QApplication::processEvents(QEventLoop::AllEvents,20);QTest::qWait(5);}}
QAction* command(MainWindow& w,const char* id){for(auto* a:w.findChildren<QAction*>())if(a->property("commandId")==id)return a;throw std::runtime_error(std::string("Missing command ")+id);}
QDialog* open(MainWindow& w,const char* id){auto* a=command(w,id);require(a->isEnabled(),"Command disabled");a->trigger();QDialog* result=nullptr;wait([&]{for(auto* d:w.findChildren<QDialog*>())if(d->isVisible()){result=d;return true;}return false;});return result;}
QPushButton* apply(QDialog& d){auto* box=d.findChild<QDialogButtonBox*>();require(box,"Missing dialog buttons");return box->button(QDialogButtonBox::Apply);}
Document fixture(){Document doc;doc.id=newId();doc.width=200;doc.height=160;Layer layer;layer.id=newId();layer.name="Effect subject";layer.transform={36,30,128,100,0,false,false,Transform::Sampling::Nearest};std::vector<Pixel> pixels(12800);for(int y=10;y<90;++y)for(int x=12;x<116;++x)pixels[size_t(y)*128+x]={uint8_t(70+x),uint8_t(30+y),uint8_t(130-x/2),255};layer.raster=Raster::fromRgba(128,100,reinterpret_cast<const uint8_t*>(pixels.data()),128*4);doc.layers={layer};return doc;}
}
int main(int argc,char** argv){QApplication app(argc,argv);ui::installVisualStyle();try{require(argc==2,"Output directory required");QDir directory(QString::fromLocal8Bit(argv[1]));require(directory.mkpath("."),"Capture directory");MainWindow window(true);auto& project=window.addProject(fixture());window.resize(1180,800);window.show();QApplication::processEvents();const auto original=*project.document;
 auto* effects=open(window,"layer.effects");auto* tabs=effects->findChild<QTabWidget*>();require(tabs&&tabs->count()==6,"Six effect editors");for(int i=0;i<tabs->count();++i){tabs->setCurrentIndex(i);auto* page=tabs->widget(i);QCheckBox* present=nullptr;for(auto* c:page->findChildren<QCheckBox*>())if(c->text().startsWith("Use "))present=c;require(present,"Effect presence control");present->setChecked(true);QApplication::processEvents();require(project.effectPreview&&project.document==original,"Effect draft mutated canonical document");require(effects->grab().save(directory.filePath(QString("effect-%1.png").arg(i))),"Effect capture");}
 apply(*effects)->click();QApplication::processEvents();require(!project.document->layers[0].effectsJson.empty()&&project.history.undoCount()==1,"Effects Apply missing undo transaction");project.canvas->setFocus();command(window,"edit.undo")->trigger();require(project.document==original,"Effects undo lost source");
 const std::array<std::pair<const char*,const char*>,9> panels{{{"adjust.black_white","black-white"},{"adjust.color_balance","color-balance"},{"adjust.invert","invert"},{"adjust.gaussian","gaussian-adjustment"},{"adjust.motion","motion-adjustment"},{"adjust.noise","noise-adjustment"},{"filter.vignette","vignette"},{"filter.bloom","bloom"},{"filter.tonal","tonal"}}};
 for(auto [id,name]:panels){project.canvas->setFocus();auto* dialog=open(window,id);wait([&]{return apply(*dialog)->isEnabled();});require(project.document==original,"Panel preview mutated canonical document");require(dialog->grab().save(directory.filePath(QString::fromLatin1(name)+".png")),"Panel capture");auto* preview=dialog->findChild<QCheckBox*>(QString::fromLatin1(id).startsWith("filter")?"filterPreviewEnabled":"adjustmentPreviewEnabled");require(preview,"Preview control");preview->setChecked(false);require(!project.effectPreview,"Preview off did not clear canvas draft");dialog->reject();QApplication::processEvents();require(project.document==original&&!project.effectPreview,"Cancel changed original");}
 Document empty;empty.id=newId();empty.width=120;empty.height=90;Layer blank;blank.id=newId();blank.transform={0,0,120,90};empty.layers={blank};auto& vignetteProject=window.addProject(empty);auto* vignette=open(window,"filter.vignette");wait([&]{return apply(*vignette)->isEnabled();});require(vignette->grab().save(directory.filePath("vignette-empty.png")),"Empty vignette capture");QPointer<QDialog> vignetteGuard=vignette;apply(*vignette)->click();wait([&]{return !vignetteGuard||!vignetteGuard->isVisible();});require(vignetteProject.document->layers[0].raster&&vignetteProject.document->layers[0].raster->pixel(0,0).a>0,"Vignette did not paint clear edge pixels");vignetteProject.canvas->setFocus();command(window,"edit.undo")->trigger();require(vignetteProject.document==empty,"Empty vignette undo did not restore blank layer");
 std::cout<<"PASS six editable effects Apply/Undo, nine adjustment/filter panels Preview/Cancel, 16 captures\n";return 0;
 }catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
