#if UINTPTR_MAX < UINT64_MAX
#error "handle_conv is unavailable on 32-bit, do not store handle_t in void*"
#endif