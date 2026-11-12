#include "SdkPortConfiguration.h"
#include "classic/rfcomm.h"
// BTstack 501e6d2's RPN_DATA_BITS_5/6/7/8 are all defined as 0 although
// rfcomm.c encodes the argument as two wire bits (0,1,2,3). In C, enum objects
// may hold any value representable by their compatible integer type. Keep this
// conversion in C: casting 1..3 to that defective enum in C++ is not portable.
// No SDK source or header is modified.
uint8_t picow_rfcomm_send_port_configuration(uint16_t cid, uint8_t baud,
    uint8_t data_bits, uint8_t stop_bits, uint8_t parity, uint8_t flow_control) {
  return rfcomm_send_port_configuration(cid, (rpn_baud_t)baud,
      (rpn_data_bits_t)data_bits, (rpn_stop_bits_t)stop_bits,
      (rpn_parity_t)parity, flow_control);
}
