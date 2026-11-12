#pragma once
#include <cstdint>

// Diagnostic format v1. Each printf supplies (unsigned long)id, unsigned line.
#define SINK_PCM_REPORT_PREFIX "SINK_PCM v=1 report=%lu line=%u/3 "

namespace sink_pcm_sample {
// One main-loop owner, process lifetime only. Never reuse an ID after wrap.
// On exhaustion leave both values unchanged and emit no framed status.
inline bool nextReportId(uint32_t &last, uint32_t &id) {
    if (last == UINT32_MAX) return false;
    id = ++last;
    return true;
}
} // namespace sink_pcm_sample
