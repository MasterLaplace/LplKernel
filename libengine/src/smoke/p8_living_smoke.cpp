#include "libengine/libengine.h"

#include <lpl/ecology/LivingRecipe.hpp>

extern "C" void libengine_living_fold(libengine_living_fold_result_t *out)
{
    if (out == nullptr)
        return;
    *out = libengine_living_fold_result_t{};

    const lpl::ecology::LivingResult run = lpl::ecology::runLiving(lpl::ecology::parityLivingRecipe());

    out->population_sig = run.populationSignature;
    out->genome_sig = run.genomeSignature;
    out->stigmergy_sig = run.stigmergySignature;
    out->social_sig = run.socialSignature;
    out->extinctions = run.extinctions;
    out->anomalies = run.anomalies;
    out->realised_rooms = run.realisedRooms;
    out->migrations = run.migrations;
    out->alpha_changes = run.alphaChanges;
    out->trail_cells = run.trailCells;
    out->living_ok = run.ok;
}
