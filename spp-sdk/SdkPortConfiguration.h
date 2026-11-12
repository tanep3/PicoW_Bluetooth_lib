#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
// Internal C bridge for the pinned SDK's incorrectly aliased RPN data-bit enum.
// Inputs must be validated by spp::validPort before this adapter boundary.
uint8_t picow_rfcomm_send_port_configuration(uint16_t cid, uint8_t baud,
    uint8_t data_bits, uint8_t stop_bits, uint8_t parity, uint8_t flow_control);
#ifdef __cplusplus
}
#endif
