#pragma once
#include <stdint.h>
#include <stdbool.h>
// Repository-authored extension declarations. Implemented only by the
// hash-checked build-directory SDK overlay, not the unmodified upstream SDK.
#ifdef __cplusplus
extern "C" {
#endif
void picow_avrcp_controller_forget_settings(uint16_t cid);
void picow_avrcp_controller_forget_custom(uint16_t cid);
void picow_avrcp_controller_custom_poll(uint16_t cid);
uint8_t picow_avrcp_cancel(uint16_t cid);
bool picow_avrcp_cancel_pending(uint16_t cid);
uint8_t picow_avrcp_cancel_status(uint16_t cid);
uint8_t picow_avrcp_controller_custom(uint16_t, uint8_t, uint8_t, uint8_t, uint8_t, uint32_t, const uint8_t *, uint16_t);
uint8_t picow_avrcp_controller_setting_attribute_text(uint16_t, uint8_t,
                                                      const uint8_t *);
uint8_t picow_avrcp_controller_setting_value_text(uint16_t, uint8_t, uint8_t,
                                                  const uint8_t *);
uint8_t picow_avrcp_controller_current_settings(uint16_t, uint8_t,
                                                const uint8_t *);
uint8_t picow_avrcp_controller_set_settings(uint16_t, uint8_t, const uint8_t *,
                                            const uint8_t *);
#ifdef __cplusplus
}
#endif
