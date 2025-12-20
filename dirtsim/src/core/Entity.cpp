#include "Entity.h"
#include "ReflectSerializer.h"

namespace DirtSim {

void to_json(nlohmann::json& j, const SparkleParticle& s)
{
    j = ReflectSerializer::to_json(s);
}

void from_json(const nlohmann::json& j, SparkleParticle& s)
{
    s = ReflectSerializer::from_json<SparkleParticle>(j);
}

void to_json(nlohmann::json& j, const Entity& e)
{
    j = ReflectSerializer::to_json(e);
}

void from_json(const nlohmann::json& j, Entity& e)
{
    e = ReflectSerializer::from_json<Entity>(j);
}

} // namespace DirtSim
