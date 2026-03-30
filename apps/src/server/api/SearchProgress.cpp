#include "SearchProgress.h"
#include "core/ReflectSerializer.h"

namespace DirtSim {
namespace Api {

void to_json(nlohmann::json& j, const SearchProgress& value)
{
    j = ReflectSerializer::to_json(value);
}

void from_json(const nlohmann::json& j, SearchProgress& value)
{
    value = ReflectSerializer::from_json<SearchProgress>(j);
}

} // namespace Api
} // namespace DirtSim
