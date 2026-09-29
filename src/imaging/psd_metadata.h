#pragma once
#include "psd_codec.h"
#include <QByteArray>
#include <QMap>
namespace compositor::imaging {
using PhotoshopExtras=QMap<QByteArray,QByteArray>;
void applyPhotoshopMetadata(Layer&,const PhotoshopExtras&,int canvasWidth,int canvasHeight,QStringList&,const ImportOptions&);
}
