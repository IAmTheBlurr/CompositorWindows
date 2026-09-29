#include "MainWindow.h"
#include "CameraRawPanel.h"
namespace compositor {
void MainWindow::cameraRaw(){auto* p=current();auto* layer=active();if(editPanel_||!p||!p->document||!layer||!layer->raster||layer->group||p->maskSelected||!layer->adjustmentJson.empty())return;auto host=makeEditPanelHost(*p,*p->document,"Camera Raw");editPanel_=openCameraRawPanel(this,*p->document,*layer,std::move(host));refresh(false,false);}
}
