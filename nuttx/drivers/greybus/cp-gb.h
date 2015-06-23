#ifndef  _CP_GB_H_
#define  _CP_GB_H_

#include <stdlib.h>

#include <nuttx/greybus/types.h>

#define GB_CP_TYPE_INVALID             0x00
#define GB_CP_TYPE_PROTOCOL_VERSION    0x01
#define GB_CP_TYPE_PROBE_AP            0x02
#define GB_CP_TYPE_CONNECTED           0x03
#define GB_CP_TYPE_DISCONNECTED        0x04
#define GB_CP_TYPE_RESPONSE            0x80// OR'd with the rest

struct gb_cp_probe_ap_request {
    __u8 endo_id;
    __u8 intf_id;
};

struct gb_cp_probe_ap_response {
    __le16 auth_size;
    __u8 auth_data[0];
};

struct gb_cp_protocol_version_request {
    __u8 offer_major;
    __u8 offer_minor;
};

struct gb_cp_protocol_version_response {
    __u8 major;
    __u8 minor;
};


#endif


