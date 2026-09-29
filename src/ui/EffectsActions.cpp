#include "MainWindow.h"
#include "EditPanelSession.h"
#include "PropertyControls.h"
#include "effects/LayerEffects.h"
#include <QCheckBox>
#include <QColorDialog>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QPixmap>
#include <QSignalBlocker>
#include <cmath>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPushButton>
#include <QTabWidget>
#include <QVBoxLayout>
#include <array>
namespace compositor {
namespace {
constexpr std::array<const char*,6> keys{"stroke","shadow","colorOverlay","innerShadow","outerGlow","innerGlow"};
constexpr std::array<const char*,6> names{"Stroke","Drop Shadow","Color Overlay","Inner Shadow","Outer Glow","Inner Glow"};
class EffectsPanel final:public ui::EditPanelSession {
    Document before_;Layer original_;QJsonObject settings_;QDialog dialog_;QCheckBox preview_{"Preview"};bool closed_{};
    std::string encoded()const{return settings_.isEmpty()?std::string{}:QJsonDocument(settings_).toJson(QJsonDocument::Compact).toStdString();}
    Document draft()const{auto doc=before_;for(auto& layer:doc.layers)if(layer.id==original_.id)layer.effectsJson=encoded();return doc;}
    void changed(){if(!closed_&&host_.preview)host_.preview(preview_.isChecked()?std::make_shared<const Document>(draft()):nullptr);}
    void finish(int answer){if(closed_)return;if(host_.closeColor)host_.closeColor(answer==QDialog::Accepted);closed_=true;try{if(answer==QDialog::Accepted&&host_.commit&&(!host_.valid||host_.valid())){auto json=encoded();effects::validateLayerEffectsJson(json);host_.commit({draft(),original_.id,false,[json](const Layer& layer){auto out=layer;out.effectsJson=json;return out;}});}}catch(const std::exception& error){if(host_.error)host_.error(QString::fromUtf8(error.what()));}if(host_.preview)host_.preview({});if(host_.closed)host_.closed();host_={};deleteLater();}
public:
    EffectsPanel(QWidget* parent,const Document& document,const Layer& layer,ui::EditPanelHost host):EditPanelSession(parent,Kind::Filter,false,std::move(host)),before_(document),original_(layer),settings_(QJsonDocument::fromJson(QByteArray::fromStdString(layer.effectsJson)).object()),dialog_(parent){
        dialog_.setObjectName("layerEffectsPanel");dialog_.setWindowTitle("Layer Effects");dialog_.setWindowFlags(Qt::Tool|Qt::WindowTitleHint|Qt::WindowCloseButtonHint);auto* layout=new QVBoxLayout(&dialog_);layout->setContentsMargins(18,16,18,16);auto* tabs=new QTabWidget;layout->addWidget(tabs);
        for(size_t i=0;i<keys.size();++i){const QString key=keys[i];auto draft=std::make_shared<QJsonObject>(settings_.value(key).toObject());auto fallback=[i](const char* field){QString f=field;if(f=="opacity")return i==1||i==3?.5:i>=4?.75:1.;if(f=="size")return i==0?4.:i==4?20.:10.;if(f=="angle")return 90.;if(f=="blur"||f=="distance")return i==1?20.:10.;return i>=4?1.:0.;};
            for(const char* field:{"opacity","red","green","blue"})if(!draft->contains(field))(*draft)[field]=fallback(field);
            auto* page=new QWidget;auto* pageLayout=new QVBoxLayout(page);auto* present=new QCheckBox(QString("Use %1").arg(names[i]));present->setChecked(settings_.value(key).isObject());pageLayout->addWidget(present);auto* fields=new QWidget;auto* form=new QFormLayout(fields);pageLayout->addWidget(fields);fields->setEnabled(present->isChecked());
            auto write=[this,key,draft,present]{if(present->isChecked())settings_[key]=*draft;else settings_.remove(key);changed();};
            connect(present,&QCheckBox::toggled,&dialog_,[fields,write](bool value){fields->setEnabled(value);write();});
            auto number=[&,draft,key,write](const char* field,const QString& label,double low,double high,int decimals=1){if(!draft->contains(field))(*draft)[field]=fallback(field);auto* spin=new ui::PropertyNumber;spin->setAccessibleName(QString(names[i])+" "+label);spin->setRange(low,high);spin->setDecimals(decimals);const double factor=QString(field)=="opacity"?100:1;spin->setRange(low*factor,high*factor);spin->setDecimals(factor==100?0:decimals);spin->setValue((*draft)[field].toDouble()*factor);spin->setFixedWidth(88);auto* row=new QWidget;auto* rowLayout=new QHBoxLayout(row);rowLayout->setContentsMargins(0,0,0,0);auto* slider=new ui::TrackSlider(Qt::Horizontal);slider->setRange(0,10000);slider->setAccessibleName(QString(names[i])+" "+label+" slider");slider->setValue(int(std::lround((spin->value()/factor-low)/(high-low)*10000)));rowLayout->addWidget(slider,1);rowLayout->addWidget(spin);form->addRow(label,row);connect(slider,&QSlider::valueChanged,&dialog_,[spin,low,high,factor](int value){spin->setValue((low+(high-low)*value/10000.)*factor);});connect(spin,&QDoubleSpinBox::valueChanged,&dialog_,[draft,field=QString(field),write,slider,low,high,factor](double value){QSignalBlocker block(slider);slider->setValue(int(std::lround((value/factor-low)/(high-low)*10000)));(*draft)[field]=value/factor;write();});};
            auto* enabled=new QCheckBox("Enabled");enabled->setChecked(draft->value("enabled").toBool(true));form->addRow(enabled);connect(enabled,&QCheckBox::toggled,&dialog_,[draft,write](bool value){(*draft)["enabled"]=value;write();});
            if(i==0||i>=4)number("size","Size (px)",0,500);if(i==1||i==3){number("angle","Angle",-360,360);number("distance","Distance (px)",0,5000);number("blur","Blur (px)",0,500);}
            number("opacity","Opacity (%)",0,1,0);
            if(i==0){auto* inside=new QCheckBox("Inside");inside->setChecked(draft->value("inside").toBool());form->addRow(inside);connect(inside,&QCheckBox::toggled,&dialog_,[draft,write](bool value){(*draft)["inside"]=value;write();});}
            auto* color=new QPushButton;auto update=[draft,color]{auto c=QColor::fromRgbF((*draft)["red"].toDouble(),(*draft)["green"].toDouble(),(*draft)["blue"].toDouble());color->setText(c.name());QPixmap swatch(18,18);swatch.fill(c);color->setIcon(QIcon(swatch));color->setIconSize({18,18});};update();form->addRow("Color",color);connect(color,&QPushButton::clicked,&dialog_,[this,draft,write,update]{if(host_.openColor){QPointer<EffectsPanel> owner=this;host_.openColor({(*draft)["red"].toDouble(),(*draft)["green"].toDouble(),(*draft)["blue"].toDouble()},"Color Picker (Layer Effect)",[owner,draft,write,update](effects_tools::PaletteColor color){if(!owner||owner->closed_)return;(*draft)["red"]=color.red;(*draft)["green"]=color.green;(*draft)["blue"]=color.blue;update();write();});return;}auto value=QColorDialog::getColor(QColor::fromRgbF((*draft)["red"].toDouble(),(*draft)["green"].toDouble(),(*draft)["blue"].toDouble()),&dialog_,"Effect Color");if(value.isValid()){(*draft)["red"]=value.redF();(*draft)["green"]=value.greenF();(*draft)["blue"]=value.blueF();update();write();}});pageLayout->addStretch();tabs->addTab(page,names[i]);
        }
        preview_.setChecked(true);layout->addWidget(&preview_);connect(&preview_,&QCheckBox::toggled,&dialog_,[this]{changed();});auto* buttons=new QDialogButtonBox(QDialogButtonBox::Apply|QDialogButtonBox::Cancel);layout->addWidget(buttons);connect(buttons->button(QDialogButtonBox::Apply),&QPushButton::clicked,&dialog_,&QDialog::accept);connect(buttons,&QDialogButtonBox::rejected,&dialog_,&QDialog::reject);connect(&dialog_,&QDialog::finished,this,[this](int answer){finish(answer);});dialog_.resize(730,390);dialog_.show();dialog_.raise();dialog_.activateWindow();
    }
    QDialog* panel()const override{return const_cast<QDialog*>(&dialog_);}bool committing()const override{return false;}void cancel()override{dialog_.reject();}
};
}
void MainWindow::editLayerEffects(){auto* project=current();auto* layer=active();if(editPanel_||!project||!project->document||!layer||!layer->raster||layer->group||!layer->adjustmentJson.empty())return;auto before=*project->document;auto original=*layer;auto host=makeEditPanelHost(*project,before,"Layer Effects");editPanel_=new EffectsPanel(this,before,original,std::move(host));refresh(false,false);}
}
