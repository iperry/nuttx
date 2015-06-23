#define CONFIG_GREYBUS_DEBUG

#include <errno.h>

#include <nuttx/config.h>
#include <nuttx/arch.h>
#include <nuttx/greybus/greybus.h>
#include <nuttx/greybus/debug.h>
#include "cp-gb.h"

#define GB_CP_VERSION_MAJOR    0x00
#define GB_CP_VERSION_MINOR    0x01

static int g_cportid;

#if defined (CONFIG_ARCH_BOARD_ARA_BDB2A_SVC)
#define SVC
#endif

#if !defined(SVC)
static uint8_t gb_cp_protocol_version(struct gb_operation *op) {
    struct gb_cp_protocol_version_response *resp;

    resp = gb_operation_alloc_response(op, sizeof(*resp));
    if (!resp) {
        return GB_OP_NO_MEMORY;
    }

    return GB_OP_SUCCESS;
}

static uint8_t gb_cp_probe_ap(struct gb_operation *operation) {
    struct gb_cp_probe_ap_response *response;

    lldbg("Received request//\n");
    response = gb_operation_alloc_response(operation, sizeof(*response) + 4);
    if (!response) {
        return GB_OP_NO_MEMORY;
    }

    response->auth_size = 4;
    response->auth_data[0] = 1;
    response->auth_data[1] = 2;
    response->auth_data[2] = 3;
    response->auth_data[3] = 4;

    lldbg("Sending response//\n");

    return GB_OP_SUCCESS;
}
#endif

/*
 * SVC only
 */
#if defined (SVC)

#if 0
static void svc_gb_cp_probe_ap_sync(struct gb_operation *op) {
    struct gb_cp_probe_ap_request *req;
    struct gb_cp_probe_ap_response *resp;

    req = gb_operation_get_request_payload(op->request);
    resp = gb_operation_get_request_payload(op);

    gb_info("probe returned: %u\n", resp->auth_size);
    gb_info("data[0]: %u\n", resp->auth_data[0]);
    gb_info("data[1]: %u\n", resp->auth_data[1]);
    gb_info("data[2]: %u\n", resp->auth_data[2]);
    gb_info("data[3]: %u\n", resp->auth_data[3]);
}
#endif

uint8_t svc_gb_cp_probe_ap(__u8 endo_id, __u8 intf_id) {
    struct gb_cp_probe_ap_request *req;
    struct gb_cp_probe_ap_response *resp;
    struct gb_operation *op_req;
    struct gb_operation *op_resp;

    op_req = gb_operation_create(g_cportid, GB_CP_TYPE_PROBE_AP, sizeof(*req));
    if (!op_req) {
        return -ENOMEM;
    }
    gb_info("op: %p\n", op_req);

    req = gb_operation_get_request_payload(op_req);
    req->endo_id = endo_id;
    req->intf_id = intf_id;

    gb_operation_send_request_sync(op_req);

    gb_info("sent probe\n");
    op_resp = gb_operation_get_response_op(op_req);
    resp = gb_operation_get_request_payload(op_resp);

    gb_info("resp: %p\n", resp);;

    gb_info("probe returned: %u\n", resp->auth_size);
    gb_info("data[0]: %u\n", resp->auth_data[0]);
    gb_info("data[1]: %u\n", resp->auth_data[1]);
    gb_info("data[2]: %u\n", resp->auth_data[2]);
    gb_info("data[3]: %u\n", resp->auth_data[3]);
    gb_operation_destroy(op_req);

    return 0;
}

/*
 * End svc
 */
#endif

static struct gb_operation_handler gb_cp_handlers[] = {
#if !defined(SVC)
    GB_HANDLER(GB_CP_TYPE_PROTOCOL_VERSION, gb_cp_protocol_version),
    GB_HANDLER(GB_CP_TYPE_PROBE_AP, gb_cp_probe_ap),
#endif
};

static int gb_cp_init(unsigned int cport) {
    (void)cport;
    /* Start the thingie here? */
    /*
     * spawn a pthread to handle cp things!
     *
     * do i need one?
     */
    return 0;
};

struct gb_driver cp_driver  = {
    .init = gb_cp_init,
    .op_handlers = (struct gb_operation_handler*) gb_cp_handlers,
    .op_handlers_count = ARRAY_SIZE(gb_cp_handlers),
};

void gb_cp_register(int cport)
{
    g_cportid = cport;

    gb_register_driver(cport, &cp_driver);
    gb_info("Registered control cport %u. Handlers: %u\n", cport, cp_driver.op_handlers_count);
}
