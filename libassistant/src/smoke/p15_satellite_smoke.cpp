/**
 * @file p15_satellite_smoke.cpp
 * @brief Gate P15 `satellite` — one protocol, three machines, one set of decisions.
 *
 * What this gate guards is not an algorithm but an AGREEMENT. A hosted development
 * node, this kernel's satellite profile and an eventual microcontroller firmware
 * share no register, no allocator and no instruction set — and two of them will not
 * even be x86. They must still decide the same thing about the same audio: when to
 * start sending, when to stop, whether the word was heard, and whether the node is
 * hearing itself.
 *
 * The audio is synthesised from the frame index alone, so no wave file has to reach
 * both sides — the same reason the world gate derives a world from a seed rather than
 * loading one.
 *
 * Must match LplAssistant/tests/test_satellite_parity.cpp on the host, bit for bit.
 *
 * @author MasterLaplace
 * @copyright MIT License
 */

#include "libassistant/libassistant.h"

#include <lpl/satellite/Parity.hpp>

extern "C" void libassistant_satellite_fold(libassistant_satellite_fold_result_t *out)
{
    if (out == nullptr)
        return;
    *out = libassistant_satellite_fold_result_t{};

    lpl::satellite::SatelliteFoldResult folded{};
    lpl::satellite::foldSatelliteState(folded);

    out->feature_sig = folded.featureSignature;
    out->level_sig = folded.levelSignature;
    out->event_sig = folded.eventSignature;
    out->wire_sig = folded.wireSignature;
    out->state_sig = folded.stateSignature;
    out->template_sig = folded.templateSignature;
    out->emitted = folded.framesEmitted;
    out->utterances = folded.utterances;
    out->detections = folded.detections;
    out->wake_frame = folded.wakeFrame;
    out->wake_distance = folded.wakeDistance;
    out->speech_distance = folded.speechDistance;
    out->echoes = folded.echoesRejected;
    out->transitions = folded.transitions;
    out->idle_permille = folded.idlePermille;
    out->duty_permille = folded.dutyPermille;
    out->tagged_audio = folded.taggedAudioIsAudio;
}
