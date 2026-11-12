#pragma once
// Shared resource ceiling for independent developer sample executables.
// Keep the combined resource headroom; capacity is not an expected link count.
#ifndef ENABLE_CLASSIC
#error Link pico_btstack_classic
#endif
// SDK compiles its stdout dump helper even when WANT_HCI_DUMP is disabled.
#define ENABLE_PRINTF_HEXDUMP
#define HAVE_EMBEDDED_TIME_MS
#define HAVE_ASSERT
#define HCI_OUTGOING_PRE_BUFFER_SIZE 4
#define HCI_ACL_PAYLOAD_SIZE (1024 + 4)
#define HCI_ACL_CHUNK_SIZE_ALIGNMENT 4
#define MAX_NR_AVDTP_CONNECTIONS 1
#define MAX_NR_AVDTP_STREAM_ENDPOINTS 1
#define MAX_NR_HCI_CONNECTIONS 2
#define MAX_NR_L2CAP_CHANNELS 12
#define MAX_NR_L2CAP_SERVICES 4
#define MAX_NR_SERVICE_RECORD_ITEMS 4
#define MAX_NR_BTSTACK_LINK_KEY_DB_MEMORY_ENTRIES 2
#define MAX_NR_CONTROLLER_ACL_BUFFERS 3
#define ENABLE_HCI_CONTROLLER_TO_HOST_FLOW_CONTROL
#define HCI_HOST_ACL_PACKET_LEN 1024
#define HCI_HOST_ACL_PACKET_NUM 3
#define HCI_HOST_SCO_PACKET_LEN 120
#define HCI_HOST_SCO_PACKET_NUM 0
#define NVM_NUM_LINK_KEYS 2
#define MAX_ATT_DB_SIZE 512
#define HCI_RESET_RESEND_TIMEOUT_MS 1000
// No synchronous printf / HCI dump in the BTstack hot path.

#define MAX_NR_RFCOMM_CHANNELS 2
#define MAX_NR_RFCOMM_MULTIPLEXERS 1
#define MAX_NR_RFCOMM_SERVICES 2

// A2DP source + two SPP records + AVRCP target = four SDP records.
#define MAX_NR_AVRCP_CONNECTIONS 4

// Pairing responses are decided by the verified fixed-peer guard.
#define ENABLE_EXPLICIT_IO_CAPABILITIES_REPLY
