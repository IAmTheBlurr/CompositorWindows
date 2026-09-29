#pragma once
#include "core/Document.h"
#include <QMimeData>
#include <QPointer>

namespace compositor::ui {
inline constexpr auto copiedLayerMime="application/x-compositor-copied-layers";
struct CopiedLayers {
    Document document;
    std::vector<std::string> roots;
    // Identity only chooses placement for Paste in the originating tab. The
    // document and its immutable rasters remain owned after that tab closes.
    QPointer<QObject> origin;
};
class CopiedLayerMimeData final:public QMimeData {
public:
    const std::shared_ptr<const CopiedLayers> layers;
    explicit CopiedLayerMimeData(std::shared_ptr<const CopiedLayers> value):layers(std::move(value)){
        setData(copiedLayerMime,"Compositor layer snapshot");
    }
};
inline std::shared_ptr<const CopiedLayers> copiedLayerSnapshot(const QMimeData* mime){
    const auto* local=dynamic_cast<const CopiedLayerMimeData*>(mime);return local?local->layers:nullptr;
}
}
