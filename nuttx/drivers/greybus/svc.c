static struct gb_operation_handler gb_svc_handlers[] = {

}

static int gb_svc_init(unsigned int cport) {
    return 0;
}

static struct gb_driver gb_svc_drv = {
    .init = gb_svc_init,
    .op_handlers = gb_svc_handlers,
    .op_counters_count = ARRAY_SIZE(gb_svc_handlers),
};

