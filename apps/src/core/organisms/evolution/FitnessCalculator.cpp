#include "FitnessCalculator.h"
#include "core/Assert.h"
#include "core/organisms/evolution/DuckEvaluator.h"
#include "core/organisms/evolution/GooseEvaluator.h"
#include "core/organisms/evolution/TreeEvaluator.h"

namespace DirtSim {

double computeFitnessForOrganism(const FitnessContext& context)
{
    switch (context.organismType) {
        case OrganismType::DUCK: {
            return DuckEvaluator::evaluate(context);
        }
        case OrganismType::GOOSE: {
            return GooseEvaluator::evaluate(context);
        }
        case OrganismType::NES_DUCK: {
            DIRTSIM_ASSERT(
                false,
                "FitnessCalculator: NES_DUCK fitness is scenario-driven; do not compute via "
                "FitnessCalculator");
            return 0.0;
        }
        case OrganismType::TREE: {
            return TreeEvaluator::evaluate(context);
        }
    }

    DIRTSIM_ASSERT(false, "FitnessCalculator: Unknown OrganismType");
    return 0.0;
}

} // namespace DirtSim
