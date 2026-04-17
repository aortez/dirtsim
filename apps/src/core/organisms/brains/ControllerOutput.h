#pragma once

namespace DirtSim {

struct ControllerOutput {
    float x = 0.0f;
    float y = 0.0f;
    bool a = false;
    bool b = false;
    float xRaw = 0.0f;
    float yRaw = 0.0f;
    float aRaw = 0.0f;
    float bRaw = 0.0f;
};

} // namespace DirtSim
