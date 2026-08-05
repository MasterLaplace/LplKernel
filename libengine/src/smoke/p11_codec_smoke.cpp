/*
** EPITECH PROJECT, 2026
** LplKernel
** File description:
** Codec parity fold — the fifth gate, and the first with two different kernels.
**
** P7 folds the world a recipe builds, P8 the simulation on it, P9 the world that
** streams, P10 the shape a grammar grows. All four compile ONE arithmetic twice and
** check the compiler did not change it.
**
** This one is different in kind. The host build of lpl-codec takes a 128-bit XOR
** kernel and this build takes a word-at-a-time one — deliberately different code
** reaching the same answer, because over GF(2) addition is associative, commutative
** and free of rounding, so the order XORs are issued in cannot matter. That is a
** claim, and it is the only claim in this repository that no other gate can test.
**
** Must match tests/parity/test_codec_parity.cpp on the host, bit for bit.
*/
#include "libengine/libengine.h"

#include <lpl/codec/Parity.hpp>

extern "C" void libengine_codec_fold(libengine_codec_fold_result_t *out)
{
    if (out == nullptr)
        return;
    *out = libengine_codec_fold_result_t{};

    // parityErasureParams is the one definition both sides read, so a parameter that
    // moves moves both sides or fails to compile.
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
