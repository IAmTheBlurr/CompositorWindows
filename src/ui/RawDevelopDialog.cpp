#include "RawDevelopDialog.h"
#include "PropertyControls.h"
#include "imaging/raw_codec.h"
#include <QDialog>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QImage>
#include <QPixmap>
#include <QMessageBox>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QTimer>
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrentRun>
#include <atomic>
namespace compositor::ui {
std::optional<imaging::DecodedImage> developRawImage(QWidget* parent,const std::filesystem::path& path,const imaging::ImportOptions& options){
    std::shared_ptr<imaging::RawFrame> frame;try{frame=std::make_shared<imaging::RawFrame>(path,options);}catch(const std::exception& error){QMessageBox::warning(parent,"Develop Camera RAW",QString::fromStdWString(path.filename().wstring())+": "+QString::fromUtf8(error.what()));return {};}auto initial=frame->settings(),settings=initial;
    QDialog dialog(parent);dialog.setObjectName("rawDevelopDialog");dialog.setWindowTitle("Develop Camera RAW — "+QString::fromStdWString(path.filename().wstring()));dialog.resize(840,700);auto* layout=new QVBoxLayout(&dialog);auto* preview=new QLabel;preview->setMinimumSize(520,330);preview->setAlignment(Qt::AlignCenter);preview->setText("Developing preview…");layout->addWidget(preview,1);auto* status=new QLabel("LibRaw · sRGB · 8-bit output after development");status->setWordWrap(true);layout->addWidget(status);auto* form=new QFormLayout;layout->addLayout(form);
    auto* asShot=new QCheckBox("Use camera white balance");asShot->setChecked(true);form->addRow(asShot);std::array<PropertyNumber*,4> numbers{};const char* labels[]{"Exposure (stops)","Temperature (K)","Tint","Tone boost"};const double lo[]{-5,2000,-150,0},hi[]{5,50000,150,1};for(int i=0;i<4;++i){auto* spin=new PropertyNumber;spin->setRange(lo[i],hi[i]);spin->setDecimals(i==1?0:2);spin->setSingleStep(i==0?.1:i==1?100:i==3?.05:1);spin->setObjectName(QString("rawDevelop_%1").arg(i));numbers[size_t(i)]=spin;form->addRow(labels[i],spin);}auto setNumbers=[&]{const double values[]{settings.exposure,settings.temperature,settings.tint,settings.boost};for(size_t i=0;i<4;++i){QSignalBlocker blocked(numbers[i]);numbers[i]->setValue(values[i]);}QSignalBlocker blocked(asShot);asShot->setChecked(settings.asShot);};setNumbers();auto* reset=new QPushButton("Reset to As Shot");form->addRow(reset);
    auto* buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);buttons->button(QDialogButtonBox::Ok)->setText("Develop and Import");layout->addWidget(buttons);auto* apply=buttons->button(QDialogButtonBox::Ok);apply->setEnabled(false);QTimer timer;timer.setSingleShot(true);timer.setInterval(160);
    struct Result{std::optional<imaging::DecodedImage> image;QString error;bool full{};};QFutureWatcher<Result> watcher;auto token=std::make_shared<std::atomic_bool>(false);bool full=false,pending=false,closed=false;uint64_t revision=0,running=0;std::optional<imaging::DecodedImage> result;
    std::function<void()> start;start=[&]{if(closed)return;if(watcher.isRunning()){pending=true;return;}pending=false;running=revision;const auto values=settings;const bool final=full;auto limits=options;limits.cancelled=[token,cancel=options.cancelled]{return token->load()||(cancel&&cancel());};status->setText(final?"Developing full resolution…":"Developing preview…");watcher.setFuture(QtConcurrent::run([frame,values,final,limits]{Result out;out.full=final;try{out.image=frame->develop(values,!final,limits);}catch(const std::exception& e){out.error=QString::fromUtf8(e.what());}return out;}));};
    auto change=[&]{if(full)return;++revision;apply->setEnabled(false);timer.start();};for(int i=0;i<4;++i)QObject::connect(numbers[size_t(i)],&QDoubleSpinBox::valueChanged,&dialog,[&,i](double value){if(i==0)settings.exposure=value;else if(i==1){settings.temperature=value;settings.asShot=false;asShot->setChecked(false);}else if(i==2){settings.tint=value;settings.asShot=false;asShot->setChecked(false);}else settings.boost=value;change();});QObject::connect(asShot,&QCheckBox::toggled,&dialog,[&](bool value){settings.asShot=value;change();});QObject::connect(reset,&QPushButton::clicked,&dialog,[&]{settings=initial;setNumbers();change();});QObject::connect(&timer,&QTimer::timeout,&dialog,start);QObject::connect(apply,&QPushButton::clicked,&dialog,[&]{full=true;apply->setEnabled(false);reset->setEnabled(false);asShot->setEnabled(false);for(auto* n:numbers)n->setEnabled(false);++revision;timer.stop();start();});QObject::connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    QObject::connect(&watcher,&QFutureWatcher<Result>::finished,&dialog,[&]{if(closed)return;auto out=watcher.result();if(pending||revision!=running){start();return;}if(!out.error.isEmpty()){status->setText(out.error);full=false;reset->setEnabled(true);asShot->setEnabled(true);for(auto* n:numbers)n->setEnabled(true);return;}if(out.full){result=std::move(out.image);dialog.accept();return;}auto& image=out.image->image;QImage q(image.pixels.data(),int(image.width),int(image.height),qsizetype(image.stride),QImage::Format_RGBA8888_Premultiplied);preview->setPixmap(QPixmap::fromImage(q.scaled(preview->size(),Qt::KeepAspectRatio,Qt::SmoothTransformation)));status->setText(QString("%1 × %2 preview · Full resolution is developed on import").arg(image.width).arg(image.height));apply->setEnabled(true);});
    timer.start(0);dialog.exec();closed=true;token->store(true);timer.stop();watcher.waitForFinished();return result;
}
}
