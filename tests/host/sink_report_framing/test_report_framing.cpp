#include "ReportFraming.h"
#include <cassert>
#include <cstdio>
#include <cstring>

int main() {
    uint32_t last = 0, id = 42;
    assert(sink_pcm_sample::nextReportId(last, id));
    assert(last == 1 && id == 1);
    assert(sink_pcm_sample::nextReportId(last, id));
    assert(last == 2 && id == 2);
    last = UINT32_MAX - 1;
    assert(sink_pcm_sample::nextReportId(last, id));
    assert(last == UINT32_MAX && id == UINT32_MAX);
    for (int i = 0; i < 3; ++i) {
        assert(!sink_pcm_sample::nextReportId(last, id));
        assert(last == UINT32_MAX && id == UINT32_MAX);
    }
    char text[80];
    for (unsigned line = 1; line <= 3; ++line) {
        const int size = std::snprintf(text, sizeof text, SINK_PCM_REPORT_PREFIX,
                                      (unsigned long)id, line);
        char expected[] = "SINK_PCM v=1 report=4294967295 line=1/3 ";
        std::strstr(expected, "line=")[5] = char('0' + line);
        assert(size == int(std::strlen(expected)));
        assert(std::strcmp(text, expected) == 0);
    }
    std::puts("report framing helper: PASS (offline only)");
}
