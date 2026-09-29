#pragma once
#include "EditPanelSession.h"
#include "effects/CameraRaw.h"
namespace compositor {
ui::EditPanelSession* openCameraRawPanel(QWidget*,const Document&,const Layer&,ui::EditPanelHost);
}
