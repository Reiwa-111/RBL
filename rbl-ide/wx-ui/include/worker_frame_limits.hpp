#pragma once

#include <cstddef>

namespace rbl::worker_protocol {

inline constexpr std::size_t MaxFrameBytes = 36U * 1024U * 1024U;
inline constexpr std::size_t MaxStdinLineBytes = 16U * 1024U;

// Worker frames are NDJSON: the trailing LF is part of the bounded frame.
inline constexpr bool FrameFits(std::size_t frame_bytes) noexcept {
    return frame_bytes <= MaxFrameBytes;
}

// The GUI receives the serialized JSON payload before SendWorkerLine appends LF.
inline constexpr bool JsonPayloadFitsFrame(std::size_t json_bytes) noexcept {
    return json_bytes < MaxFrameBytes;
}

inline constexpr bool StdinLineFits(std::size_t line_bytes) noexcept {
    return line_bytes <= MaxStdinLineBytes;
}

} // namespace rbl::worker_protocol
