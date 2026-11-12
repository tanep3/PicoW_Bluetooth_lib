#include "Application.h"
namespace avrcp_media_sample {
int runConsole() {
    stdio_init_all();
    static Application app({{AVRCP_MEDIA_PEER_BYTES}}, AVRCP_MEDIA_ALLOW_PAIRING != 0);
    CommandParser parser;
    Binding input_binding{};
    uint32_t last_banner = 0;
    for (;;) {
        app.tick();
        const auto ms = nowMs();
        if (!app.attempted() && uint32_t(ms - last_banner) >= 3000) {
            std::puts("INERT: START initializes radio; no automatic pairing or media command.");
            last_banner = ms;
        }
        for (unsigned i = 0; i < 16; ++i) {
            const int ch = getchar_timeout_us(0);
            if (ch == PICO_ERROR_TIMEOUT) break;
            if (parser.empty()) input_binding = app.binding();
            app.command(parser.feed(ch), input_binding);
        }
        sleep_ms(1);
    }
}
}
int main() {
    // Default returns before stdio, owner construction, watchdog or radio.
    // Ordinary if keeps the enabled implementation type-checked.
    if (!AVRCP_MEDIA_ENABLE_RADIO) return 0;
    return avrcp_media_sample::runConsole();
}
