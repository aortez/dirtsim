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

void to_json(nlohmann::json& j, const ScenarioVideoFrame& frame)
{
    std::vector<uint8_t> pixelBytes;
    pixelBytes.reserve(frame.pixels.size());
    for (const std::byte value : frame.pixels) {
        pixelBytes.push_back(std::to_integer<uint8_t>(value));
    }

    j = nlohmann::json{ { "width", frame.width },
                        { "height", frame.height },
                        { "frame_id", frame.frame_id },
                        { "pixels", std::move(pixelBytes) } };
}

void from_json(const nlohmann::json& j, ScenarioVideoFrame& frame)
{
    j.at("width").get_to(frame.width);
    j.at("height").get_to(frame.height);
    j.at("frame_id").get_to(frame.frame_id);

    const std::vector<uint8_t> pixelBytes = j.at("pixels").get<std::vector<uint8_t>>();
    frame.pixels.resize(pixelBytes.size());
    for (size_t i = 0; i < pixelBytes.size(); ++i) {
        frame.pixels[i] = static_cast<std::byte>(pixelBytes[i]);
    }
}

} // namespace DirtSim
