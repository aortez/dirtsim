#include "SearchCompleted.h"
#include "core/ReflectSerializer.h"

namespace DirtSim {
namespace Api {

void to_json(nlohmann::json& j, const SearchCompleted& value)
{
    j = ReflectSerializer::to_json(value);
}

void from_json(const nlohmann::json& j, SearchCompleted& value)
{
    value = ReflectSerializer::from_json<SearchCompleted>(j);
}

} // namespace Api
} // namespace DirtSim
