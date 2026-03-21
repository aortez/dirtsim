#include "MaterialColor.h"

#include "Assert.h"
#include "ColorNames.h"

namespace DirtSim {

uint32_t getLegacyMaterialColor(Material::EnumType material)
{
    switch (material) {
        case Material::EnumType::Air:
            return ColorNames::air();
        case Material::EnumType::Dirt:
            return ColorNames::dirt();
        case Material::EnumType::Leaf:
            return ColorNames::leaf();
        case Material::EnumType::Metal:
            return ColorNames::metal();
        case Material::EnumType::Root:
            return ColorNames::root();
        case Material::EnumType::Sand:
            return ColorNames::sand();
        case Material::EnumType::Seed:
            return ColorNames::seed();
        case Material::EnumType::Wall:
            return ColorNames::stone();
        case Material::EnumType::Water:
            return ColorNames::water();
        case Material::EnumType::Wood:
            return ColorNames::wood();
    }

    DIRTSIM_ASSERT(false, "Unhandled material color");
    return ColorNames::white();
}

} // namespace DirtSim
