#pragma once

#include <cstdint>

enum class Command : uint8_t {
    GET = 1,
    SET = 2,
    DEL = 3,
    EXPIRE = 4,
    REPLICA_SYNC = 5
};