#pragma once
#include "btstack_tlv.h"
#include "hal_flash_bank.h"
#ifdef __cplusplus
extern "C" {
#endif
// One instance, synchronous main-loop ownership only. No automatic formatting.
typedef enum {
    PICOW_TLV_UNAVAILABLE, PICOW_TLV_BLANK, PICOW_TLV_READY,
    PICOW_TLV_READ_ONLY, PICOW_TLV_AMBIGUOUS, PICOW_TLV_EXHAUSTED
} picow_tlv_state_t;
typedef struct {
    picow_tlv_state_t state;
    int readable; // A validated snapshot exists for explicit recovery, not authentication.
    unsigned write_failures; // Sticky; includes failed delete (void BTstack API).
    unsigned capacity_failures;
} picow_tlv_status_t;
// Reads only. Unknown/legacy/corrupt media is never erased or converted.
const btstack_tlv_t *picow_committed_tlv_init(const hal_flash_bank_t *hal, void *hal_context);
picow_tlv_status_t picow_committed_tlv_status(void);
// Explicit recovery view ONLY. A mixed/unknown newer bank may supersede these
// bytes. Never feed this view to normal authentication or pairing callbacks.
int picow_committed_tlv_recovery_get(uint32_t tag, uint8_t *buffer, uint32_t capacity);
// Explicit application authorization to provision ONLY two fully erased banks.
// Call after explicit HAL initialization/platform readiness, before runtime
// initialization or pairing. Refuses any nonblank byte. No erase.
int picow_committed_tlv_provision_blank(void);
#ifdef __cplusplus
}
#endif
