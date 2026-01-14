# Genetic Evolution System

## Overview

This document describes the genetic algorithm for evolving neural network brains in tree organisms. The system uses mutation-only evolution with tournament selection and elitist replacement.

## Algorithm Summary

```
INITIALIZE population P with random genomes

FOR each generation:
    FOR each genome g in P:
        EVALUATE fitness(g) via individual simulation

    SELECT parents using tournament selection

    MUTATE parents to produce offspring

    REPLACE population using elitist strategy

RETURN best genome
```

## Components

### Population

A collection of `Genome` objects, each representing the weights of a `NeuralNetBrain`.

```cpp
struct EvolutionConfig {
    int population_size = 50;
    int elite_count = 3;
    int tournament_size = 3;
    int max_generations = 100;
};
```

### Evaluation

Each organism is tested individually in a calm nursery environment. Simulation runs for up to 10 minutes of simulation time.

**Fitness function** (multiplicative):

```cpp
double evaluate(const Genome& genome, World& world) {
    Tree tree = spawn_tree(genome, world);

    constexpr double MAX_TIME = 600.0;  // 10 minutes sim time.
    constexpr double ENERGY_REFERENCE = 100.0;

    double max_energy = 0.0;
    double elapsed = 0.0;

    while (elapsed < MAX_TIME && tree.isAlive()) {
        world.step();
        elapsed += world.deltaTime();
        max_energy = std::max(max_energy, tree.getEnergy());
    }

    double lifespan_score = elapsed / MAX_TIME;
    double energy_score = max_energy / ENERGY_REFERENCE;

    // Multiplicative: must survive AND grow.
    return lifespan_score * (1.0 + energy_score);
}
```

**Why multiplicative?**
- Additive fitness allows maximizing one metric while ignoring others.
- Multiplicative forces balance: a tree that dies instantly scores 0, a tree that survives but never grows scores low.
- Only trees that both survive and accumulate energy score well.

**Evaluation environment:**
- Calm nursery (no falling debris, stable conditions).
- Consistent starting position and resources.
- Each organism tested in isolation (no competition).

### Selection: Tournament

Tournament selection picks parents for the next generation.

```cpp
Genome tournament_select(
    const std::vector<Genome>& population,
    const std::vector<double>& fitness,
    int tournament_size,
    std::mt19937& rng)
{
    std::uniform_int_distribution<int> dist(0, population.size() - 1);

    int best_idx = dist(rng);
    double best_fitness = fitness[best_idx];

    for (int i = 1; i < tournament_size; i++) {
        int idx = dist(rng);
        if (fitness[idx] > best_fitness) {
            best_idx = idx;
            best_fitness = fitness[idx];
        }
    }

    return population[best_idx];
}
```

**Why tournament selection?**
- Simple to implement and tune.
- Selection pressure adjustable via tournament size (k).
- Robust to noisy fitness evaluations.
- No global sorting or probability calculation required.
- Parallelizes trivially.

**Tournament size guidelines:**
- k=2: Gentle pressure, maintains diversity.
- k=3-5: Moderate pressure (recommended starting point).
- k=7+: Strong pressure, faster convergence but less exploration.

### Mutation

No crossover. Offspring are mutated copies of selected parents.

```cpp
struct MutationConfig {
    double rate = 0.015;        // Probability each weight is mutated.
    double sigma = 0.05;        // Gaussian noise standard deviation.
    double reset_rate = 0.0005; // Rare full weight reset.
};

Genome mutate(const Genome& parent, const MutationConfig& cfg, std::mt19937& rng) {
    Genome child = parent;

    std::normal_distribution<double> noise(0.0, cfg.sigma);
    std::uniform_real_distribution<double> coin(0.0, 1.0);

    for (size_t i = 0; i < child.weights.size(); i++) {
        double r = coin(rng);
        if (r < cfg.reset_rate) {
            // Full reset (rare) - helps escape local optima.
            child.weights[i] = noise(rng) * 2.0;
        } else if (r < cfg.reset_rate + cfg.rate) {
            // Gaussian perturbation (common).
            child.weights[i] += noise(rng);
        }
    }

    return child;
}
```

**Why mutation-only (no crossover)?**
- Neural networks have permutation symmetry ("competing conventions problem").
- Hidden neuron #3 in parent A might encode the same concept as neuron #7 in parent B.
- Naive crossover creates incompatible weight combinations.
- Proper crossover requires alignment mechanisms (e.g., NEAT innovation numbers).
- Mutation-only is simpler and proven effective for fixed-topology networks.

**Mutation parameters:**
- `rate = 0.015`: ~1,800 weights mutated per offspring (of ~120K total).
- `sigma = 0.05`: Small perturbations relative to Xavier-initialized weights.
- `reset_rate = 0.0005`: ~60 weights fully reset per offspring.

### Replacement: Elitist

Generational replacement with elitism.

```cpp
std::vector<Genome> replace(
    const std::vector<Genome>& parents,
    const std::vector<double>& parent_fitness,
    const std::vector<Genome>& offspring,
    const std::vector<double>& offspring_fitness,
    const EvolutionConfig& cfg)
{
    // Combine parents and offspring.
    std::vector<std::pair<double, Genome>> pool;
    for (size_t i = 0; i < parents.size(); i++) {
        pool.push_back({parent_fitness[i], parents[i]});
    }
    for (size_t i = 0; i < offspring.size(); i++) {
        pool.push_back({offspring_fitness[i], offspring[i]});
    }

    // Sort by fitness (descending).
    std::sort(pool.begin(), pool.end(),
        [](const auto& a, const auto& b) { return a.first > b.first; });

    // Take top population_size.
    std::vector<Genome> next_generation;
    for (int i = 0; i < cfg.population_size; i++) {
        next_generation.push_back(pool[i].second);
    }

    return next_generation;
}
```

**Why elitist replacement?**
- Best solutions never lost (parents compete with offspring).
- Balances exploitation (keep good solutions) with exploration (new offspring).
- Elite count emerges naturally from (μ+λ) selection.

**Generation flow:**
1. Evaluate all individuals.
2. Select parents via tournament (with replacement) to produce offspring.
3. Mutate each selected parent to create offspring.
4. Combine parents + offspring, keep top N.

## Complete Algorithm

```cpp
std::vector<Genome> evolve(
    const EvolutionConfig& cfg,
    const MutationConfig& mut_cfg,
    std::mt19937& rng)
{
    // Initialize random population.
    std::vector<Genome> population;
    for (int i = 0; i < cfg.population_size; i++) {
        population.push_back(Genome::random(rng));
    }

    for (int gen = 0; gen < cfg.max_generations; gen++) {
        // Evaluate fitness.
        std::vector<double> fitness;
        for (const auto& genome : population) {
            World world = create_nursery_world();
            fitness.push_back(evaluate(genome, world));
        }

        // Log generation statistics.
        double best = *std::max_element(fitness.begin(), fitness.end());
        double avg = std::accumulate(fitness.begin(), fitness.end(), 0.0) / fitness.size();
        log_generation(gen, best, avg);

        // Select and mutate to create offspring.
        std::vector<Genome> offspring;
        std::vector<double> offspring_fitness;

        int num_offspring = cfg.population_size * 2;  // Produce 2x population.
        for (int i = 0; i < num_offspring; i++) {
            Genome parent = tournament_select(population, fitness, cfg.tournament_size, rng);
            Genome child = mutate(parent, mut_cfg, rng);

            World world = create_nursery_world();
            double child_fitness = evaluate(child, world);

            offspring.push_back(child);
            offspring_fitness.push_back(child_fitness);
        }

        // Replace population.
        population = replace(population, fitness, offspring, offspring_fitness, cfg);
    }

    return population;
}
```

## Configuration Defaults

```cpp
// Evolution.
population_size = 50
elite_count = 3           // Implicit via (μ+λ) selection.
tournament_size = 3
max_generations = 100

// Mutation.
rate = 0.015
sigma = 0.05
reset_rate = 0.0005

// Evaluation.
max_simulation_time = 600.0  // 10 minutes.
energy_reference = 100.0
```

## Future Enhancements

### Reproduction as Fitness

Instead of artificial fitness scoring, trees that produce seeds pass on their genomes:

```cpp
// Seeds inherit (mutated) parent genome.
// Offspring survival becomes implicit fitness.
```

This moves toward true evolutionary simulation where fitness is survival and reproduction, not a computed score.

### Parallel Evaluation

For faster evolution, evaluate multiple organisms in parallel:

```cpp
// Batch parallel (generational).
#pragma omp parallel for
for (int i = 0; i < population.size(); i++) {
    fitness[i] = evaluate(population[i], create_world());
}
```

Or switch to steady-state replacement for pipeline parallelism (workers submit results asynchronously, no sync barriers).

### Adaptive Mutation

Allow mutation parameters to evolve:

```cpp
struct Genome {
    std::vector<double> weights;
    double sigma;  // Self-adaptive mutation magnitude.
};
```

Individuals that find good sigma values thrive.

### Additional Fitness Metrics

| Metric | Purpose |
|--------|---------|
| `final_cell_count` | Reward structural growth |
| `root_leaf_ratio` | Encourage balanced trees |
| `damage_survived` | Reward resilience |
| `seeds_produced` | Reward reproduction |
| `offspring_survival` | Quality of reproduction |

### Harsh Environments

Evolve in challenging conditions (falling debris, floods, competition) to produce more robust behaviors.

## Related Documents

- `evolution-framework.md` - Overall architecture (states, repository, API).
- `plant.md` - Tree organism design and brain interface.
