#include "libknowledge/libknowledge.h"

#include <lpl/knowledge/KnowledgePack.hpp>
#include <lpl/knowledge/ParityKnowBlob.hpp>

extern "C" uint32_t libknowledge_image_bytes(void) { return lpl::knowledge::kParityKnowledgeImageSize; }

extern "C" const char *libknowledge_image_state(void)
{
    lpl::knowledge::KnowledgePack pack;
    switch (pack.open(lpl::knowledge::kParityKnowledgeImage, lpl::knowledge::kParityKnowledgeImageSize))
    {
    case lpl::knowledge::OpenStatus::Ok: return "embedded";
    default: return "malformed";
    }
}
