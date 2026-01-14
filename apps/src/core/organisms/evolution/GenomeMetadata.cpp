#include "GenomeMetadata.h"
#include "core/ReflectSerializer.h"

namespace DirtSim {

void to_json(nlohmann::json& j, const GenomeMetadata& meta)
{
    j = ReflectSerializer::to_json(meta);
}

void from_json(const nlohmann::json& j, GenomeMetadata& meta)
{
    meta = ReflectSerializer::from_json<GenomeMetadata>(j);
}

} // namespace DirtSim
