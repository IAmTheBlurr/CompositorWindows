#include "NumericScrub.h"
#include <QApplication>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QLabel>
#include <QLayout>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QPointer>
#include <cmath>

namespace compositor::ui {
namespace {
class NumericScrub final:public QObject {
    QPointer<QWidget> owner_,label_;
    QPointer<QAbstractSpinBox> field_;
    std::function<void(bool)> transaction_;
    double start_{},origin_{};bool editing_{};
    static QAbstractSpinBox* numeric(QWidget*widget){if(auto*spin=qobject_cast<QAbstractSpinBox*>(widget))return spin;if(!widget)return nullptr;auto fields=widget->findChildren<QAbstractSpinBox*>();return fields.size()==1?fields.front():nullptr;}
    static QAbstractSpinBox* field(QLabel*label){
        if(auto*spin=numeric(label->buddy()))return spin;
        auto*layout=label->parentWidget()?label->parentWidget()->layout():nullptr;if(!layout)return nullptr;
        const int index=layout->indexOf(label);if(index<0)return nullptr;
        for(int i=index+1;i<layout->count();++i){auto*w=layout->itemAt(i)->widget();if(auto*s=numeric(w))return s;if(qobject_cast<QLabel*>(w))break;}return nullptr;
    }
    void finish(bool cancel){if(!field_)return;if(cancel){if(auto*spin=qobject_cast<QDoubleSpinBox*>(field_))spin->setValue(start_);else if(auto*spin=qobject_cast<QSpinBox*>(field_))spin->setValue(int(start_));}if(editing_&&transaction_)transaction_(false);editing_=false;field_.clear();if(label_){label_->releaseMouse();label_->unsetCursor();}label_.clear();}
public:
    NumericScrub(QWidget*owner,std::function<void(bool)>transaction):QObject(owner),owner_(owner),transaction_(std::move(transaction)){qApp->installEventFilter(this);}
    ~NumericScrub()override{if(qApp)qApp->removeEventFilter(this);}
    bool eventFilter(QObject*object,QEvent*event)override{
        auto*widget=qobject_cast<QWidget*>(object);if(!widget||!owner_||(widget->window()!=owner_&&!owner_->isAncestorOf(widget)))return false;
        if(field_){
            if(event->type()==QEvent::KeyPress&&static_cast<QKeyEvent*>(event)->key()==Qt::Key_Escape){finish(true);return true;}
            if(event->type()==QEvent::MouseMove){auto*mouse=static_cast<QMouseEvent*>(event);const double factor=mouse->modifiers().testFlag(Qt::ShiftModifier)?10:mouse->modifiers().testFlag(Qt::AltModifier)?.1:1;double step=1;if(auto*spin=qobject_cast<QDoubleSpinBox*>(field_))step=spin->singleStep();const double value=start_+(mouse->globalPosition().x()-origin_)*step*factor;if(auto*spin=qobject_cast<QDoubleSpinBox*>(field_))spin->setValue(value);else if(auto*spin=qobject_cast<QSpinBox*>(field_))spin->setValue(int(std::round(value)));return true;}
            if(event->type()==QEvent::MouseButtonRelease){finish(false);return true;}
            if(event->type()==QEvent::WindowDeactivate||event->type()==QEvent::Hide){finish(false);}
        }
        auto*label=qobject_cast<QLabel*>(widget);if(!label)return false;auto*spin=field(label);if(!spin||!spin->isEnabled()||spin->isReadOnly())return false;
        if(event->type()==QEvent::Enter){label->setCursor(Qt::SizeHorCursor);label->setToolTip("Drag to adjust · Shift: faster · Alt: finer");}
        if(event->type()==QEvent::MouseButtonPress){auto*mouse=static_cast<QMouseEvent*>(event);if(mouse->button()!=Qt::LeftButton)return false;field_=spin;label_=label;origin_=mouse->globalPosition().x();if(auto*number=qobject_cast<QDoubleSpinBox*>(spin))start_=number->value();else if(auto*number=qobject_cast<QSpinBox*>(spin))start_=number->value();editing_=widget->window()==owner_;if(editing_&&transaction_)transaction_(true);label->grabMouse();return true;}return false;
    }
};
}
void installNumericScrubbing(QWidget*owner,std::function<void(bool)>transaction){new NumericScrub(owner,std::move(transaction));}
}
