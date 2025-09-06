#pragma once

#include <cstdint>

namespace ulmd {

/**
 * @brief Parsed view of 64-byte ULMD message
 * @invariant All fields valid only if parse_ulmd() returned true
 */
struct MsgView {
    uint32_t magic;
    uint8_t version;
    uint8_t msg_type;
    uint16_t flags;
    uint64_t seq_no;
    uint64_t send_ts_ns;
    char symbol[8];
    int64_t bid_px;
    uint32_t bid_sz;
    int64_t ask_px;
    uint32_t ask_sz;
    uint32_t reserved;
    uint32_t crc32;
};

/**
 * @brief Parse 64-byte ULMD message from wire format
 * @param data Raw message buffer (must be exactly 64 bytes)
 * @param view Output parsed message view
 * @return true if valid (magic/version/CRC correct), false otherwise
 * @invariant view contents undefined if return false
 * @invariant Validates magic=ULMD, version=1, CRC32 over bytes [0..59]
 * @performance O(1) parsing with single CRC pass
 */
bool parse_ulmd(const void* data, MsgView& view);

} // namespace ulmd