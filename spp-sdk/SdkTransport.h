#pragma once
#include <stdint.h>
// Implemented only by the hash-checked SDK transport overlay. Mode values
// match Bluetooth L2CAP: BASIC=0, ERTM=3. Missing/not-open returns 0xff.
#ifdef __cplusplus
extern "C" {
#endif
uint8_t picow_rfcomm_channel_mode(uint16_t rfcomm_cid);
#ifdef __cplusplus
}
#endif
