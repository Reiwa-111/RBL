#include "worker_frame_limits.hpp"

#include <iostream>

using namespace rbl::worker_protocol;

static_assert(FrameFits(0));
static_assert(FrameFits(MaxFrameBytes - 1));
static_assert(FrameFits(MaxFrameBytes));
static_assert(!FrameFits(MaxFrameBytes + 1));

// A JSON payload of MaxFrameBytes-1 plus its LF is exactly the worker ceiling.
static_assert(JsonPayloadFitsFrame(MaxFrameBytes - 1));
static_assert(!JsonPayloadFitsFrame(MaxFrameBytes));

static_assert(StdinLineFits(MaxStdinLineBytes));
static_assert(!StdinLineFits(MaxStdinLineBytes + 1));

int main() {
    std::cout << "Worker NDJSON/stdin frame boundaries: PASS (exact 36 MiB LF-inclusive frame, +1 rejection, 16 KiB stdin)\n";
    return 0;
}
