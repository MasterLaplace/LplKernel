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
