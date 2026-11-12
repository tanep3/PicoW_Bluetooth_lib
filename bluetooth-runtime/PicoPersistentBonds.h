#pragma once
#include <cstdint>
#include "classic/btstack_link_key_db.h"

// Explicit opt-in. Uses the already initialized global TLV provider; reserves
// tags 0x50424b00..0x50424b07. No flash address is selected by this class.
// Single controller, main-loop ownership, no concurrent access/rebinding.
class PicoPersistentBonds {
public:
    static constexpr unsigned Capacity = 8;
    enum Error : uint32_t {
        Unavailable = 1, InvalidRecord = 2, WriteFailed = 4,
        DeleteFailed = 8, SequenceExhausted = 16, InvalidKeyType = 32, ForeignController = 64
    };
    struct Status {
        bool ready; // Callbacks + controller (and READY protected provider). Not physical durability.
        uint32_t errors; // Sticky until explicitly cleared; never contains keys.
        uint32_t stores; // Counters acknowledge current-view readback only.
        uint32_t deletes;
        uint32_t evictions;
    };
    // Pass to BluetoothRuntime::setLinkKeyDatabase BEFORE any profile begin.
    // Merely retrieving this table does not touch flash or opt the runtime in.
    static const btstack_link_key_db_t *database();
    static Status status();
    static void clearErrors(); // Does not repair records, reset counters, or reopen.
};
