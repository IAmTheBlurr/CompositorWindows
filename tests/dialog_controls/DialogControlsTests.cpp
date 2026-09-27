#include "ui/VisualStyle.h"
#include "ui/ProjectOpenDialog.h"
#include "ui/PaletteDialog.h"
#include "ui/MainWindow.h"
#include <QApplication>
#include <QAbstractButton>
#include <QDialogButtonBox>
#include <QElapsedTimer>
#include <QDir>
#include <QFileDialog>
#include <QMessageBox>
#include <QColorDialog>
#include <QInputDialog>
#include <QLabel>
#include <QProgressBar>
#include <QProgressDialog>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QToolButton>
#include <iostream>
#include <stdexcept>

using namespace compositor;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void visibleIcon(QAbstractButton* button){
    require(button&&!button->icon().isNull(),"Button has an icon");
    const auto icon=button->icon();const auto size=button->size();
    const auto painted=button->grab().toImage().convertToFormat(QImage::Format_RGB32);
    button->setIcon({});button->resize(size);
    const auto blank=button->grab().toImage().convertToFormat(QImage::Format_RGB32);
    button->setIcon(icon);
    int pixels=0;QRect bounds;
    for(int y=0;y<painted.height();++y)for(int x=0;x<painted.width();++x)
        if(painted.pixel(x,y)!=blank.pixel(x,y)){++pixels;bounds=bounds.united(QRect(x,y,1,1));}
    std::cout<<button->objectName().toStdString()<<" icon pixels="<<pixels<<" bounds="<<bounds.width()<<'x'<<bounds.height()<<'\n';
    require(pixels>=20&&bounds.width()>=5&&bounds.height()>=5,"Icon remains legible inside actual styled button");
}
void capture(QWidget& widget,const QString& root,const QString& name){require(widget.grab().save(root+'/'+name+".png"),"Save dialog capture");}
void filePicker(const QString& root,const QString& title,QFileDialog::AcceptMode mode){
    QFileDialog dialog(nullptr,title,root);dialog.setAcceptMode(mode);
    dialog.setFileMode(mode==QFileDialog::AcceptOpen?QFileDialog::ExistingFiles:QFileDialog::AnyFile);
    dialog.resize(680,480);dialog.show();QTest::qWait(60);
    int count=0;
    for(auto* button:dialog.findChildren<QToolButton*>())if(button->isVisible()){
        require(!button->toolTip().isEmpty()||!button->accessibleName().isEmpty(),"File button has an accessible label");
        visibleIcon(button);++count;
    }
    require(count==6,"All six file navigation and view buttons inspected");
    capture(dialog,root,title);dialog.reject();
}
void editorDialog(MainWindow& window,const QString& root,const char* id){
    QAction* action=nullptr;for(auto* candidate:window.findChildren<QAction*>())if(candidate->property("commandId").toString()==id&&candidate->isVisible())action=candidate;
    require(action&&action->isEnabled(),"Dialog command available");
    QTimer driver;driver.setInterval(10);QElapsedTimer elapsed;elapsed.start();bool seen=false;QString failure;
    QObject::connect(&driver,&QTimer::timeout,&window,[&]{
        if(elapsed.elapsed()<120)return;
        for(auto* dialog:window.findChildren<QDialog*>())if(dialog->isVisible()){
            driver.stop();seen=true;
            try{
                for(auto* button:dialog->findChildren<QAbstractButton*>())if(button->isVisible()&&button->text().isEmpty()&&!button->icon().isNull())visibleIcon(button);
                capture(*dialog,root,QString::fromLatin1(id));
            }catch(const std::exception& error){failure=QString::fromUtf8(error.what());}
            dialog->reject();return;
        }
    });
    driver.start();action->trigger();while(!seen&&elapsed.elapsed()<3000)QTest::qWait(10);driver.stop();
    require(seen,"Editor dialog displayed");if(!failure.isEmpty())throw std::runtime_error(failure.toStdString());QTest::qWait(20);
}
}
int main(int argc,char** argv){
    QApplication app(argc,argv);app.setOrganizationName("CompositorDialogFixture");app.setApplicationName("DialogControls");
    QTemporaryDir settings;QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settings.path());
    try{
        require(argc==2,"Provide capture directory");const auto root=QString::fromLocal8Bit(argv[1]);QDir().mkpath(root);
        ui::installVisualStyle();
        MainWindow window(true);Document document;document.id=newId();document.width=64;document.height=48;
        Layer layer;layer.id=newId();layer.name="Preview";layer.transform={0,0,64,48};layer.raster=Raster::filled(64,48,{70,100,150,255});document.layers.push_back(layer);
        window.addProject(document,"Dialog preview");window.show();
        for(bool mac:{false,true}){
            ui::setMacTitleBarEnabled(mac);const auto out=root+(mac?"/mac":"/windows");QDir().mkpath(out);
            filePicker(out,"Import Images",QFileDialog::AcceptOpen);
            filePicker(out,"Save Compositor Project",QFileDialog::AcceptSave);
            filePicker(out,"Export Image",QFileDialog::AcceptSave);
            QString failure;bool seen=false;
            QTimer::singleShot(20,[&]{for(auto* widget:QApplication::topLevelWidgets())if(auto* dialog=qobject_cast<QDialog*>(widget);dialog&&dialog->isVisible()){
                seen=true;try{QAbstractButton* up=nullptr;for(auto* button:dialog->findChildren<QAbstractButton*>())if(button->accessibleName()=="Parent folder")up=button;visibleIcon(up);capture(*dialog,out,"Open project");}catch(const std::exception& error){failure=QString::fromUtf8(error.what());}dialog->reject();}});
            ui::chooseProjectDirectories(nullptr);require(seen,"Project picker inspected");if(!failure.isEmpty())throw std::runtime_error(failure.toStdString());
            PaletteDialog palette({.2,.4,.6},"Color");palette.show();QTest::qWait(20);capture(palette,out,"Color");palette.reject();
            for(auto kind:{QMessageBox::Information,QMessageBox::Warning,QMessageBox::Critical,QMessageBox::Question}){
                QMessageBox message(kind,"Compositor","Dialog icon and button preview",QMessageBox::Ok|QMessageBox::Cancel);
                message.show();QTest::qWait(20);require(!message.iconPixmap().isNull(),"Message symbol present");capture(message,out,"Message-"+QString::number(kind));message.reject();
            }
            QColorDialog color(Qt::blue);color.show();QTest::qWait(20);capture(color,out,"System color");color.reject();
            QInputDialog input;input.setLabelText("Name");input.setTextValue("Layer 1");input.show();QTest::qWait(20);capture(input,out,"Rename");input.reject();
            input.setInputMode(QInputDialog::DoubleInput);input.setLabelText("Scale (%)");input.setDoubleValue(100);input.show();QTest::qWait(20);capture(input,out,"Scale");input.reject();
            QProgressDialog progress("Importing images…","Cancel",0,100);progress.setValue(50);progress.show();QTest::qWait(20);
            for(auto* child:progress.findChildren<QWidget*>(QString{},Qt::FindDirectChildrenOnly))if(qobject_cast<QLabel*>(child)||qobject_cast<QProgressBar*>(child)||qobject_cast<QPushButton*>(child)){
                require(child->isVisible()&&progress.childAt(child->geometry().center())==child,"Progress label, bar and Cancel are not covered by chrome");
            }
            capture(progress,out,"Progress");auto* cancel=progress.findChild<QPushButton*>();require(cancel,"Progress Cancel present");QTest::mouseClick(cancel,Qt::LeftButton);require(progress.wasCanceled(),"Progress Cancel works");
            for(const auto* id:{"canvas.size","canvas.image_size","layer.rename","transform.scale","adjust.hue_saturation","adjust.levels","adjust.curves","adjust.exposure","adjust.gradient_map","adjust.grain","filter.gaussian","filter.motion","filter.noise","filter.lens","file.export","app.about","help.check_updates"})editorDialog(window,out,id);
        }
        std::cout<<"PASS dialog navigation, icon rendering, both title bar modes and dialog captures\n";return 0;
    }catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}
}
