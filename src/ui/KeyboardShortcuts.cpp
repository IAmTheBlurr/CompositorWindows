#include "CommandRegistry.h"
#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QKeyEvent>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QTableWidget>
#include <QVBoxLayout>
#include <map>

namespace compositor::ui {
namespace {
struct CanvasShortcut {QString id,label;QKeySequence original;};
const std::vector<CanvasShortcut>& canvasShortcuts(){
    static const auto values=[](){std::vector<CanvasShortcut> v;
        auto add=[&](const QString&id,const QString&label,const QString&key){v.push_back({"canvas."+id,label,QKeySequence(key)});};
        add("cycle","Cycle tool mode","Tab");add("hand","Temporary Hand (hold)","Space");add("delete","Delete selection, layer or point","Delete");add("apply","Apply canvas edit","Return");add("cancel","Cancel canvas edit","Esc");
        add("smaller","Decrease brush size","[");add("larger","Increase brush size","]");add("softer","Decrease hardness","Shift+[");add("harder","Increase hardness","Shift+]");add("blendPrevious","Previous blend mode","Shift+-");add("blendNext","Next blend mode","Shift+=");add("shape","Cycle shape kind","Shift+U");
        for(int digit=0;digit<10;++digit)add("opacity"+QString::number(digit),"Opacity digit "+QString::number(digit),QString::number(digit));
        for(const auto*direction:{"Left","Right","Up","Down"})for(const auto*prefix:{"","Shift+","Ctrl+","Ctrl+Shift+"})add(QString(prefix)+direction,QString(prefix).isEmpty()?QString("Nudge %1").arg(direction):QString("%1%2").arg(prefix,direction),QString(prefix)+direction);
        return v;
    }();return values;
}
QKeySequence assigned(const QString&id,const QKeySequence&fallback){QSettings settings;auto key="shortcuts/"+id;return settings.contains(key)?QKeySequence(settings.value(key).toString(),QKeySequence::PortableText):fallback;}
}
void CommandRegistry::showShortcutEditor(){
    QDialog dialog(owner_);dialog.setObjectName("keyboardShortcutsDialog");dialog.setWindowTitle("Keyboard Shortcuts");dialog.resize(620,650);QVBoxLayout layout(&dialog);QLabel info("Choose one key with optional modifiers. Clear a field to disable its shortcut.");info.setWordWrap(true);layout.addWidget(&info);QTableWidget table;table.setColumnCount(2);table.setHorizontalHeaderLabels({"Command","Shortcut"});table.horizontalHeader()->setSectionResizeMode(0,QHeaderView::Stretch);table.horizontalHeader()->setSectionResizeMode(1,QHeaderView::ResizeToContents);table.verticalHeader()->hide();layout.addWidget(&table);
    struct Row{QString id;QKeySequence original;QKeySequenceEdit*editor;};std::vector<Row> rows;std::map<QString,bool> seen;
    auto add=[&](QString id,QString label,QKeySequence original){if(seen.contains(id))return;seen[id]=true;const int row=table.rowCount();table.insertRow(row);auto*item=new QTableWidgetItem(label);item->setFlags(item->flags()&~Qt::ItemIsEditable);table.setItem(row,0,item);auto*editor=new QKeySequenceEdit(assigned(id,original));editor->setMaximumSequenceLength(1);editor->setClearButtonEnabled(true);table.setCellWidget(row,1,editor);rows.push_back({id,original,editor});};
    for(const auto&entry:entries_)if(entry.action)add(entry.spec.id,entry.spec.menu+" / "+entry.spec.label,QKeySequence(entry.spec.windowsShortcut));
    for(const auto&key:canvasShortcuts())add(key.id,"Canvas / "+key.label,key.original);
    QDialogButtonBox buttons(QDialogButtonBox::Save|QDialogButtonBox::Cancel|QDialogButtonBox::RestoreDefaults);layout.addWidget(&buttons);connect(&buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);connect(buttons.button(QDialogButtonBox::RestoreDefaults),&QPushButton::clicked,&dialog,[&]{for(auto&row:rows)row.editor->setKeySequence(row.original);});
    connect(&buttons,&QDialogButtonBox::accepted,&dialog,[&]{std::map<QString,QString> keys;for(const auto&row:rows){const auto key=row.editor->keySequence().toString(QKeySequence::PortableText);if(key.isEmpty())continue;if(auto found=keys.find(key);found!=keys.end()&&found->second!=row.id){QMessageBox::warning(&dialog,"Shortcut conflict",QString("%1 is assigned to more than one command.").arg(key));return;}keys[key]=row.id;}
        QSettings settings;for(const auto&row:rows){if(row.editor->keySequence()==row.original)settings.remove("shortcuts/"+row.id);else settings.setValue("shortcuts/"+row.id,row.editor->keySequence().toString(QKeySequence::PortableText));}
        for(const auto&entry:entries_)if(entry.action){entry.action->setShortcut(assigned(entry.spec.id,QKeySequence(entry.spec.windowsShortcut)));if(entry.spec.id=="edit.redo"&&!settings.contains("shortcuts/edit.redo"))entry.action->setShortcuts({QKeySequence("Ctrl+Shift+Z"),QKeySequence("Ctrl+Y")});}
        dialog.accept();});dialog.exec();
}
bool CommandRegistry::translateShortcut(QObject*object,QEvent*event){
    if(translatingShortcut_||!owner_||(event->type()!=QEvent::KeyPress&&event->type()!=QEvent::KeyRelease))return false;
    auto*widget=qobject_cast<QWidget*>(object);auto*focus=QApplication::focusWidget();if(!widget||!focus||focus->window()!=owner_||isTextEditingWidget(focus)||QApplication::activeModalWidget())return false;
    auto*key=static_cast<QKeyEvent*>(event);const auto chord=QKeySequence(key->keyCombination());
    for(const auto&binding:canvasShortcuts()){auto effective=assigned(binding.id,binding.original);if(effective!=binding.original&&!effective.isEmpty()&&chord==effective){const auto original=binding.original[0];QKeyEvent mapped(event->type(),original.key(),original.keyboardModifiers(),key->text(),key->isAutoRepeat(),key->count());translatingShortcut_=true;QApplication::sendEvent(widget,&mapped);translatingShortcut_=false;event->accept();return true;}}
    bool remapped=false;
    for(const auto&binding:canvasShortcuts())if(chord==binding.original&&assigned(binding.id,binding.original)!=binding.original)remapped=true;
    for(const auto&entry:entries_){QKeySequence original(entry.spec.windowsShortcut);if(!original.isEmpty()&&chord==original&&assigned(entry.spec.id,original)!=original)remapped=true;}
    if(remapped){event->accept();return true;}return false;
}
}
