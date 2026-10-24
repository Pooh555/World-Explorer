#pragma once

#include "world_explorer/core/types.hpp"

namespace world_explorer {

using ObjectObservationId = TypedId<struct object_observation_tag>;
using ObjectTrackId = TypedId<struct object_track_tag>;
using AssetProductId = TypedId<struct asset_product_tag>;
using AssetId = TypedId<struct asset_tag>;
using AssetInstanceId = TypedId<struct asset_instance_tag>;
using AssetMaterialVariantId = TypedId<struct asset_material_variant_tag>;

}  // namespace world_explorer
