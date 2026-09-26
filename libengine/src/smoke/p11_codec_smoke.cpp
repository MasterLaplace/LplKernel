#include "libengine/libengine.h"

#include <lpl/codec/Parity.hpp>

extern "C" void libengine_codec_fold(libengine_codec_fold_result_t *out)
{
    if (out == nullptr)
        return;
    *out = libengine_codec_fold_result_t{};

    lpl::codec::CodecFoldResult folded{};
    lpl::codec::foldCodecState(folded);

    out->soliton_sig = folded.solitonSignature;
    out->droplet_sig = folded.dropletSignature;
    out->matrix_sig = folded.matrixSignature;
    out->payload_sig = folded.payloadSignature;
    out->emitted = folded.emitted;
    out->delivered = folded.delivered;
    out->peeled_blocks = folded.peeledBlocks;
    out->eliminated_blocks = folded.eliminatedBlocks;
    out->recovered = folded.recovered;
    out->vector_kernel = folded.vectorKernel;
}
