#pragma once
#include "core/Document.h"
#include <string_view>
namespace compositor::effects {
void validateLayerEffectsJson(std::string_view);
// Source pixels and the placed mask form the silhouette before effects are drawn.
// The returned raster includes the halo and is placed by a proportional transform.
Layer renderedLayerEffects(const Layer&,const LayerRenderPreview* preview=nullptr);
double layerEffectsMargin(std::string_view);
}
