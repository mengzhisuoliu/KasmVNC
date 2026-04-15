#pragma once

#include "rdr/OutStream.h"

namespace rfb::encoders {

#if defined(DEBUG_ENCODERS)
#define DEBUG_LOG(log, ...) \
    do { (log).debug(__VA_ARGS__); } while(0)
#else
#define DEBUG_LOG(log, ...)
#endif

    void write_compact(rdr::OutStream *os, int value);
} // namespace rfb::encoders
