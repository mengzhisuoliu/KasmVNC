#pragma once

#include "rdr/OutStream.h"

namespace rfb::encoders {

#define DEBUG_ENCODERS 1

#if DEBUG_ENCODERS
#define DEBUG_LOG(log, ...) \
    log.debug(__VA_ARGS__)
#else
#define DEBUG_LOG(log, ...)
#endif

    void write_compact(rdr::OutStream *os, int value);
} // namespace rfb::encoders
