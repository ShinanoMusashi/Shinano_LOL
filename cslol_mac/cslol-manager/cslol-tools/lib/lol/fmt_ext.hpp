#pragma once
#include <fmt/format.h>
#include "hash/xxh64.hpp"

// Tell {fmt} how to print lol::hash::Xxh64
template<>
struct fmt::formatter<lol::hash::Xxh64> : fmt::formatter<uint64_t> {
    template<typename FormatContext>
    auto format(const lol::hash::Xxh64& h, FormatContext& ctx) const {
        // cast to the underlying 64-bit value; parent class handles "{:#016x}" etc.
        return fmt::formatter<uint64_t>::format(static_cast<uint64_t>(h), ctx);
    }
};
