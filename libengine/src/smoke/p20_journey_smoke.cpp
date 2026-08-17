/*
** EPITECH PROJECT, 2026
** LplKernel
** File description:
** Journey parity fold — a named body that walks, and what it earns.
**
** The first gate whose subject is somebody who MOVED. Every earlier history
** signature folds a corpus being reasoned about; this one folds where a body
** ended up, and how much of what a corpus claims the run reproduced without
** being told.
**
** That distinction is the whole measurement. `Divergence` credits a run only for
** events it emitted as `Cause::Emergent` — an event the timeline caused cannot
** count as agreement with that timeline — so a run that forced everything would
** score perfectly and prove nothing.
**
** Must match tests/parity/test_history_parity.cpp on the host, bit for bit.
*/
#include "libengine/libengine.h"

#include <lpl/engine/systems/Journey.hpp>
#include <lpl/history/Parity.hpp>

extern "C" void libengine_journey_fold(libengine_journey_fold_result_t *out)
{
    if (out == nullptr)
        return;
    *out = libengine_journey_fold_result_t{};

    lpl::engine::systems::JourneyFoldResult folded{};
    lpl::engine::systems::foldJourneyState(folded);

    out->chronicle_sig = folded.chronicleSignature;
    out->position_sig = folded.positionSignature;
    out->deed_sig = folded.deedSignature;
    out->seeded = folded.seeded;
    out->forced = folded.forced;
    out->unplaceable = folded.unplaceable;
    out->arrivals = folded.arrivals;
    out->scored = folded.scoredClaims;
    out->earned = folded.earned;
    out->score = folded.divergenceScore;
    out->first_arrival = folded.firstArrival;
    out->routed = folded.routedLegs;
    out->avoided = folded.avoided;
    out->road_sig = folded.roadSignature;
    out->waypoint_sig = folded.waypointSignature;
    out->road_cells = folded.roadCells;
    out->road_pairs = folded.roadPairs;
    out->alt_chronicle = folded.alternateChronicle;
    out->alt_arrivals = folded.alternateArrivals;
    out->alt_first = folded.alternateFirst;
}
