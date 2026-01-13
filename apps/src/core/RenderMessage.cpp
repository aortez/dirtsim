#include "RenderMessage.h"
#include "ReflectSerializer.h"

namespace DirtSim {

void to_json(nlohmann::json& j, const BasicCell& cell)
{
    j = ReflectSerializer::to_json(cell);
}

void from_json(const nlohmann::json& j, BasicCell& cell)
{
    cell = ReflectSerializer::from_json<BasicCell>(j);
}

void to_json(nlohmann::json& j, const BoneData& bone)
{
    j = ReflectSerializer::to_json(bone);
}

void from_json(const nlohmann::json& j, BoneData& bone)
{
    bone = ReflectSerializer::from_json<BoneData>(j);
}

void to_json(nlohmann::json& j, const DebugCell& cell)
{
    j = ReflectSerializer::to_json(cell);
}

void from_json(const nlohmann::json& j, DebugCell& cell)
{
    cell = ReflectSerializer::from_json<DebugCell>(j);
}

void to_json(nlohmann::json& j, const OrganismData& org)
{
    j = ReflectSerializer::to_json(org);
}

void from_json(const nlohmann::json& j, OrganismData& org)
{
    org = ReflectSerializer::from_json<OrganismData>(j);
}

} // namespace DirtSim
