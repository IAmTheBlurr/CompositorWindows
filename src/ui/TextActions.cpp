#include "MainWindow.h"
#include "EditorIcons.h"
#include "PropertyControls.h"
#include <QColorDialog>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFontComboBox>
#include <QFontDatabase>
#include <QFormLayout>
#include <QLabel>
#include <QMenuBar>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QTimer>
#include <QVBoxLayout>
#include <QKeyEvent>
#include <algorithm>
#include <cmath>

namespace compositor {
namespace {
class TextInput final:public QPlainTextEdit {
public:
    std::function<void()> apply;
    std::function<void(int,double)> spacing;
    explicit TextInput(QWidget* parent):QPlainTextEdit(parent){}
protected:
    void keyPressEvent(QKeyEvent* event) override {
        if((event->key()==Qt::Key_Return||event->key()==Qt::Key_Enter)&&event->modifiers().testFlag(Qt::ControlModifier)){if(apply)apply();return;}
        if(event->modifiers().testFlag(Qt::AltModifier)&&spacing){const double step=event->modifiers().testFlag(Qt::ShiftModifier)?10:1;
            switch(event->key()){case Qt::Key_Left:spacing(0,-step);return;case Qt::Key_Right:spacing(0,step);return;case Qt::Key_Up:spacing(1,-step);return;case Qt::Key_Down:spacing(1,step);return;default:break;}}
        QPlainTextEdit::keyPressEvent(event);
    }
};
class TextPanel final:public QDialog {
    editing::TextStyle style_;
    TextInput* input_{};
    QLabel* error_{};
    QPushButton* apply_{};
    QTimer* timer_{};
    std::function<void(const editing::TextStyle&)> preview_;
    std::function<void(const editing::TextStyle&)> commit_;
    bool publish(){
        timer_->stop();style_.content=input_->toPlainText();
        try{if(!style_.valid())throw std::runtime_error("Text must contain at most 100,000 UTF-16 units and valid dimensions.");preview_(style_);error_->clear();apply_->setEnabled(true);return true;}
        catch(const std::exception& error){error_->setText(QString::fromUtf8(error.what()));apply_->setEnabled(false);return false;}
    }
    void changed(){timer_->start();}
    void apply(){if(!publish())return;try{if(commit_)commit_(style_);accept();}catch(const std::exception& error){error_->setText(QString::fromUtf8(error.what()));}}
public:
    const editing::TextStyle& style()const{return style_;}
    void setCommit(std::function<void(const editing::TextStyle&)> value){commit_=std::move(value);}
    TextPanel(editing::TextStyle style,std::function<void(const editing::TextStyle&)> preview,QWidget* parent):QDialog(parent),style_(std::move(style)),preview_(std::move(preview)){
        setObjectName("textEditorPanel");setWindowTitle("Edit Text");setWindowModality(Qt::WindowModal);setAttribute(Qt::WA_DeleteOnClose);resize(420,570);
        auto* layout=new QVBoxLayout(this);auto* hint=new QLabel("Text updates on the canvas. Ctrl+Enter applies; Esc cancels.");hint->setWordWrap(true);layout->addWidget(hint);
        input_=new TextInput(this);input_->setObjectName("textContent");input_->setAccessibleName("Text content");input_->setPlainText(style_.content);input_->setMinimumHeight(150);layout->addWidget(input_,1);
        auto* form=new QFormLayout;layout->addLayout(form);auto* family=new QFontComboBox;family->setObjectName("textFont");family->setAccessibleName("Font family");family->setCurrentFont(editing::textFont(style_));form->addRow("Font",family);
        auto* face=new QComboBox;face->setObjectName("textFace");face->setAccessibleName("Font face");face->addItems(QFontDatabase::styles(family->currentFont().family()));face->setCurrentText(editing::textFont(style_).styleName());form->addRow("Face",face);
        auto number=[&](const QString& label,const char* name,double low,double high,double value){auto* spin=new ui::PropertyNumber;spin->setObjectName(name);spin->setAccessibleName(label);spin->setRange(low,high);spin->setDecimals(2);spin->setValue(value);form->addRow(label,spin);return spin;};
        auto* size=number("Size (px)","textSize",1,2000,style_.fontSize);auto* tracking=number("Tracking (px)","textTracking",-100,1000,style_.tracking);auto* leading=number("Line spacing (px)","textLeading",0,5000,style_.leading);leading->setSpecialValueText("Auto (120%)");
        auto* alignment=new QComboBox;alignment->setObjectName("textAlignment");alignment->setAccessibleName("Paragraph alignment");alignment->addItems({"Left","Center","Right"});alignment->setCurrentIndex(int(style_.alignment));form->addRow("Alignment",alignment);
        auto* color=new QPushButton("Choose Color…");color->setObjectName("textColor");form->addRow("Color",color);
        auto* box=new QCheckBox("Fixed paragraph box");box->setObjectName("textParagraphBox");box->setChecked(style_.boxSize.has_value());form->addRow(box);
        const auto dimensions=style_.boxSize.value_or(editing::textBoxSize(style_));auto* width=number("Box width (px)","textBoxWidth",16,30000,dimensions.width());auto* height=number("Box height (px)","textBoxHeight",16,30000,dimensions.height());width->setEnabled(box->isChecked());height->setEnabled(box->isChecked());
        error_=new QLabel;error_->setWordWrap(true);error_->setObjectName("textError");layout->addWidget(error_);
        auto* buttons=new QDialogButtonBox(QDialogButtonBox::Apply|QDialogButtonBox::Cancel);apply_=buttons->button(QDialogButtonBox::Apply);apply_->setObjectName("textApply");apply_->setText("Apply Text");layout->addWidget(buttons);
        timer_=new QTimer(this);timer_->setSingleShot(true);timer_->setInterval(60);connect(timer_,&QTimer::timeout,this,[this]{publish();});
        connect(input_,&QPlainTextEdit::textChanged,this,[this]{changed();});
        connect(family,&QFontComboBox::currentFontChanged,this,[=,this](const QFont& font){QSignalBlocker block(face);face->clear();face->addItems(QFontDatabase::styles(font.family()));face->setCurrentText("Regular");style_.fontName=font.family();changed();});
        connect(face,&QComboBox::currentTextChanged,this,[=,this](const QString& value){style_.fontName=family->currentFont().family()+(value.isEmpty()||value=="Regular"?QString{}:"-"+value);changed();});
        connect(size,&QDoubleSpinBox::valueChanged,this,[this](double value){style_.fontSize=value;changed();});connect(tracking,&QDoubleSpinBox::valueChanged,this,[this](double value){style_.tracking=value;changed();});connect(leading,&QDoubleSpinBox::valueChanged,this,[this](double value){style_.leading=value;changed();});connect(alignment,&QComboBox::currentIndexChanged,this,[this](int value){style_.alignment=editing::TextAlignment(value);changed();});
        connect(color,&QPushButton::clicked,this,[this]{const auto value=QColorDialog::getColor(QColor::fromRgbF(style_.red,style_.green,style_.blue),this,"Text color");if(value.isValid()){style_.red=value.redF();style_.green=value.greenF();style_.blue=value.blueF();changed();}});
        auto updateBox=[=,this]{width->setEnabled(box->isChecked());height->setEnabled(box->isChecked());style_.boxSize=box->isChecked()?std::optional<QSizeF>({width->value(),height->value()}):std::nullopt;changed();};
        connect(box,&QCheckBox::toggled,this,[=]{updateBox();});connect(width,&QDoubleSpinBox::valueChanged,this,[=]{updateBox();});connect(height,&QDoubleSpinBox::valueChanged,this,[=]{updateBox();});
        connect(apply_,&QPushButton::clicked,this,[this]{apply();});connect(buttons,&QDialogButtonBox::rejected,this,&QDialog::reject);
        input_->apply=[this]{apply();};input_->spacing=[=,this](int which,double change){if(which==0)tracking->setValue(tracking->value()+change);else leading->setValue(std::max(1.,style_.lineHeight()+change));};
        input_->setFocus();input_->selectAll();changed();
    }
};
}
void MainWindow::setupTextActions(){
    QMenu* menu=nullptr;for(auto* action:menuBar()->actions())if(action->text().remove('&')=="Layer")menu=action->menu();if(!menu)return;
    menu->addSeparator();action(menu,"Edit Text Layer…",{},[this]{if(auto* layer=active();layer&&!layer->textJson.empty())openTextEditor({},layer->id);});
    action(menu,"Rasterize Text Layer",{},[this]{if(auto* layer=active();layer&&!layer->textJson.empty())edit("Rasterize Text",[this](Document&){active()->textJson.clear();});});
}
bool MainWindow::beginText(Point point,Qt::KeyboardModifiers modifiers){
    if(tool_!=Tool::Type)return false;if(!canEditLayers()||!std::isfinite(point.x)||!std::isfinite(point.y))return true;
    textPress_=point;textHitId_.clear();
    std::unordered_set<std::string> visible;for(const auto& entry:layers::entries(*current()->document))if(entry.visible)visible.insert(entry.id);
    if(!modifiers.testFlag(Qt::AltModifier))for(auto it=current()->document->layers.rbegin();it!=current()->document->layers.rend();++it){if(it->textJson.empty()||!visible.contains(it->id))continue;const auto unit=it->transform.toUnit(point);if(unit.x>=0&&unit.x<=1&&unit.y>=0&&unit.y<=1){textHitId_=it->id;break;}}
    return true;
}
bool MainWindow::updateText(Point point,Qt::KeyboardModifiers,bool finish){
    if(tool_!=Tool::Type||!textPress_)return false;
    const auto start=*textPress_;if(!finish){if(textHitId_.empty()&&canvas())canvas()->setCropOverlay(editing::dragBox(start,point,false,false));return true;}
    textPress_.reset();if(canvas())canvas()->setCropOverlay({});const auto id=std::exchange(textHitId_,{});
    std::optional<editing::Rect> box;const auto bounds=editing::dragBox(start,point,false,false);if(id.empty()&&bounds.width>=4&&bounds.height>=4)box=editing::Rect{bounds.x,bounds.y,std::max(16.,std::round(bounds.width)),std::max(16.,std::round(bounds.height))};
    openTextEditor(start,id,box);return true;
}
void MainWindow::openTextEditor(Point point,const std::string& id,std::optional<editing::Rect> box){
    auto* project=current();if(!project||!project->document||textPanel_||!canEditLayers())return;
    const auto original=*project->document;const auto found=std::find_if(original.layers.begin(),original.layers.end(),[&](const Layer& layer){return layer.id==id;});
    std::optional<Layer> source;if(found!=original.layers.end()&&!found->textJson.empty())source=*found;
    auto style=source?editing::decodeTextStyle(source->textJson):textStyle_;
    if(!source){style.content.clear();style.red=foreground_.redF();style.green=foreground_.greenF();style.blue=foreground_.blueF();style.boxSize=box?std::optional<QSizeF>({box->width,box->height}):std::nullopt;}
    const Point origin=box?Point{box->x,box->y}:Point{point.x-editing::TextStyle::padding,point.y-editing::textFirstBaseline(style)};
    const std::string newIdValue=newId();const auto activeId=project->active;
    auto prepare=[original,source,origin,newIdValue,activeId](const editing::TextStyle& updated){
        auto prepared=original;if(source){for(auto& layer:prepared.layers)if(layer.id==source->id){layer=editing::restyleText(*source,updated);break;}}
        else {auto layer=editing::createTextLayer(origin,updated);layer.id=newIdValue;const auto at=std::find_if(prepared.layers.begin(),prepared.layers.end(),[&](const Layer& item){return item.id==activeId;});const auto insertion=at==prepared.layers.end()?prepared.layers.size():size_t(at-prepared.layers.begin())+1;if(at!=prepared.layers.end())layer.parentId=at->group?at->id:at->parentId;prepared.layers.insert(prepared.layers.begin()+insertion,std::move(layer));}validateDocument(prepared);return prepared;
    };
    auto* panel=new TextPanel(style,[project,prepare](const editing::TextStyle& updated){project->textPreview=std::make_shared<const Document>(prepare(updated));project->canvas->update();},this);textPanel_=panel;
    panel->setCommit([this,project,source,newIdValue](const editing::TextStyle& updated){
        if(!project->textPreview)throw std::runtime_error("Text preview is unavailable");
        if(source||!updated.content.trimmed().isEmpty()){
            auto prepared=*project->textPreview;const auto previousSelected=project->selected;const bool previousMask=project->maskSelected;
            try{edit(source?"Edit Text":"New Text Layer",[&](Document& document){document=std::move(prepared);project->active=source?source->id:newIdValue;project->selected={project->active};project->maskSelected=false;});}
            catch(...){project->selected=previousSelected;project->maskSelected=previousMask;throw;}
        }
        textStyle_=updated;textStyle_.content="Text";textStyle_.boxSize.reset();
    });
    connect(panel,&QDialog::finished,this,[this,project](int){
        project->textPreview.reset();textPanel_=nullptr;
        refresh();project->canvas->setFocus();
    });
    panel->open();const auto right=mapToGlobal(QPoint(width()-panel->width()-32,80));panel->move(right);refresh(false,false);
}
void MainWindow::cancelText(){textPress_.reset();textHitId_.clear();if(textPanel_)textPanel_->reject();}
}
