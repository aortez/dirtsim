#include "GenomeRepository.h"

#include "core/organisms/brains/Genome.h"

namespace DirtSim {

void GenomeRepository::store(GenomeId id, const Genome& genome, const GenomeMetadata& meta)
{
    genomes_[id] = genome;
    metadata_[id] = meta;
}

bool GenomeRepository::exists(GenomeId id) const
{
    return genomes_.find(id) != genomes_.end();
}

std::optional<Genome> GenomeRepository::get(GenomeId id) const
{
    auto it = genomes_.find(id);
    if (it == genomes_.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::optional<GenomeMetadata> GenomeRepository::getMetadata(GenomeId id) const
{
    auto it = metadata_.find(id);
    if (it == metadata_.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::vector<std::pair<GenomeId, GenomeMetadata>> GenomeRepository::list() const
{
    std::vector<std::pair<GenomeId, GenomeMetadata>> result;
    result.reserve(metadata_.size());
    for (const auto& [id, meta] : metadata_) {
        result.emplace_back(id, meta);
    }
    return result;
}

void GenomeRepository::remove(GenomeId id)
{
    genomes_.erase(id);
    metadata_.erase(id);

    // Clear best if we removed it.
    if (bestId_ && *bestId_ == id) {
        bestId_ = std::nullopt;
    }
}

void GenomeRepository::clear()
{
    genomes_.clear();
    metadata_.clear();
    bestId_ = std::nullopt;
}

void GenomeRepository::markAsBest(GenomeId id)
{
    if (genomes_.find(id) != genomes_.end()) {
        bestId_ = id;
    }
}

std::optional<GenomeId> GenomeRepository::getBestId() const
{
    return bestId_;
}

std::optional<Genome> GenomeRepository::getBest() const
{
    if (!bestId_) {
        return std::nullopt;
    }
    return get(*bestId_);
}

size_t GenomeRepository::count() const
{
    return genomes_.size();
}

bool GenomeRepository::empty() const
{
    return genomes_.empty();
}

} // namespace DirtSim
