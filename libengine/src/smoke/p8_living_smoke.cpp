/*
** EPITECH PROJECT, 2026
** LplKernel
** File description:
** Living simulation parity fold — the determinism gate for a world that is
** RUNNING rather than one that was generated.
**
** Runs lpl::ecology::parityLivingRecipe() and folds the four subsystems it
** exercises. The Linux oracle tests/parity/test_living_parity.cpp runs the same
** recipe from the same constexpr definition; a bit-for-bit match proves that
** population dynamics, heredity, stigmergy and the social layer are as
** deterministic across targets as terrain generation and physics already are.
**
** Nothing here is float-dependent in the folded path: every parameter struct's
** core::f32 knobs are converted once through Fixed32::fromFloat at the top of
** each pass, exactly as procgen does, and the folds consume raw Q16.16 words.
*/
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
