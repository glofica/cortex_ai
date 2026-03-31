
/// @file goldilocks.cpp
/// @brief Goldilocks Prime Field Arithmetic Implementation
///
/// Implements modular arithmetic over p = 2^64 - 2^32 + 1 using
/// native 64-bit operations. The key insight is the reduction identity:
///   2^64 ≡ 2^32 - 1 (mod p)
/// which allows replacing division by shifts and subtractions.

#include "goldilocks.hpp"
#include <sstream>
#include <iomanip>

#ifdef _MSC_VER
#include <intrin.h>  // _umul128 for MSVC
#endif

namespace glofica {
namespace cortex {

// =============================================================================
// Reduction (The Core Engine)
// =============================================================================

uint64_t Goldilocks::reduce128(uint64_t lo, uint64_t hi) {
    // We need to compute (hi * 2^64 + lo) mod p
    // Using the identity: 2^64 ≡ 2^32 - 1 (mod p)
    // So: hi * 2^64 ≡ hi * (2^32 - 1) (mod p)
    //                 = hi * 2^32 - hi
    //
    // Result = lo + hi * 2^32 - hi (mod p)
    //        = lo + (hi << 32) - hi (mod p)
    
    // Step 1: hi_shifted = hi * 2^32 (split into parts to avoid overflow)
    uint64_t hi_lo = hi & 0xFFFFFFFFULL;        // Lower 32 bits of hi
    uint64_t hi_hi = hi >> 32;                    // Upper 32 bits of hi
    
    // hi * 2^32 = hi_lo * 2^32 + hi_hi * 2^64
    // But hi_hi * 2^64 ≡ hi_hi * (2^32 - 1), so we recurse
    // For practical values from multiplication of two 64-bit numbers,
    // hi < 2^64, so hi_hi < 2^32, and the recursion terminates.
    
    // Simpler approach: work with intermediate values and conditionally subtract
    uint64_t hi_shift = (hi << 32) - hi;  // This can underflow if hi > hi<<32
    
    // Actually, let's use a cleaner multi-step reduction.
    // hi * (2^32 - 1) = hi * 2^32 - hi
    // hi << 32 might overflow if hi >= 2^32, so we need to be careful.
    
    // Split hi = hi_upper * 2^32 + hi_lower
    // hi * 2^32 = hi_upper * 2^64 + hi_lower * 2^32
    // hi_upper * 2^64 ≡ hi_upper * (2^32 - 1) mod p
    
    // So: hi * (2^32 - 1) ≡ hi_upper*(2^32-1) + hi_lower*2^32 - hi  mod p
    //                      = hi_upper*2^32 - hi_upper + hi_lower*2^32 - hi  mod p
    //                      = (hi_upper + hi_lower)*2^32 - hi_upper - hi  mod p
    //                      = hi*2^32 - hi_upper - hi  ... wait, that's circular
    
    // Let's use a simpler, proven approach:
    // result = lo - hi + (hi << 32)
    // Handle the carries/borrows with conditional additions of MODULUS
    
    // Compute term1 = (hi << 32) which can overflow
    // hi_lo = hi & 0xFFFFFFFF, hi_hi = hi >> 32
    // hi << 32 = hi_lo << 32 (fits in 64 bits since hi_lo < 2^32)
    //          + hi_hi << 64 ≡ hi_hi * (2^32 - 1) mod p
    //          = hi_hi * 2^32 - hi_hi
    // So: hi * 2^32 mod p = (hi_lo << 32) + (hi_hi << 32) - hi_hi  mod p
    //                     = ((hi_lo + hi_hi) << 32) - hi_hi  mod p
    
    // Final: result = lo + ((hi_lo + hi_hi) << 32) - hi_hi - hi  mod p
    //               = lo + ((hi_lo + hi_hi) << 32) - hi_hi - hi_lo - hi_hi  mod p  
    //               = lo + ((hi_lo + hi_hi) << 32) - hi_lo - 2*hi_hi  mod p
    
    // This is getting complex. Let's use the standard iterative approach instead.
    // It's what Plonky2 and other production systems use.
    
    // Standard approach: just do the reduction step and handle overflow
    // r = lo + hi * (2^32 - 1) mod p
    // Since hi * 2^32 can overflow uint64, we handle it carefully
    
    uint64_t r = lo;
    
    // Process hi in two 32-bit chunks
    // Chunk 1: hi_lo * (2^32 - 1)
    uint64_t t1 = hi_lo * 0xFFFFFFFFULL;  // hi_lo < 2^32, so this fits in 64 bits
    
    // Add t1 to r with overflow detection
    uint64_t sum1 = r + t1;
    bool carry1 = (sum1 < r);
    r = sum1;
    
    // Chunk 2: hi_hi * (2^32 - 1) * 2^32 = hi_hi * (2^64 - 2^32)
    //        ≡ hi_hi * (-MODULUS + 1 - 2^32 + MODULUS) ...
    // Actually: hi_hi * 2^64 ≡ hi_hi * (2^32 - 1) mod p
    // And we already multiplied by (2^32-1), so:
    // hi_hi * (2^32-1) * (2^32) ... this is getting recursive.
    
    // Let's just use the simple approach that Polygon uses:
    // For hi_hi (which is at most 2^32-1):
    // hi_hi * (2^32-1) fits in 64 bits (max ~2^64 - 2^33 + 1)
    uint64_t t2 = hi_hi * 0xFFFFFFFFULL;
    // But this term is shifted by 32, so: t2 * 2^32
    // t2 * 2^32 might overflow, but t2 < 2^64, so t2_lo = t2 & 0xFFFFFFFF
    // t2 * 2^32 = (t2 & 0xFFFFFFFF) << 32 + (t2 >> 32) * 2^64
    //           ≡ (t2 & 0xFFFFFFFF) << 32 + (t2 >> 32) * (2^32 - 1) mod p
    
    // OK, this recursive approach is correct but complex in implementation.
    // Production code (like Plonky2) uses inline assembly or compiler intrinsics.
    // For clarity and correctness, let's use a simple but correct approach:
    
    // RESET: Use the simplest correct reduction
    // We compute: (hi * 2^64 + lo) mod p
    // = (hi * (p - 1 + 2^32) + lo) mod p   [since 2^64 = p + 2^32 - 1]
    // = (hi * (2^32 - 1) + lo) mod p        [since hi*p ≡ 0 mod p]
    // = lo + hi*2^32 - hi mod p
    
    // But hi*2^32 can overflow 64 bits if hi >= 2^32.
    // In our case, hi comes from multiplying two values < p < 2^64,
    // so hi < 2^64. We need to handle the overflow.
    
    // SIMPLE CORRECT APPROACH: Use __int128 where available, 
    // or multi-word arithmetic on MSVC
    
    // Since we're on MSVC (Windows), and _umul128 gives us hi:lo,
    // let's do the reduction using 128-bit arithmetic via two 64-bit values.
    
    // Approach from Goldilocks paper:
    // Let z = hi:lo (128-bit number)
    // z mod p, where p = 2^64 - 2^32 + 1
    //
    // Step 1: z = lo + hi * 2^64
    //         ≡ lo + hi * (2^32 - 1)  mod p
    //
    // Let c = hi >> 32  (upper 32 bits of hi)
    // Let d = hi & mask  (lower 32 bits of hi)
    //
    // hi * (2^32 - 1) = (c*2^32 + d) * (2^32 - 1)
    //                 = c*2^64 - c*2^32 + d*2^32 - d
    //                 ≡ c*(2^32-1) - c*2^32 + d*2^32 - d  mod p
    //                 = c*2^32 - c - c*2^32 + d*2^32 - d
    //                 = -c + d*2^32 - d
    //                 = d*(2^32 - 1) - c
    //
    // So: z ≡ lo + d*(2^32-1) - c  mod p
    // d < 2^32 and (2^32-1) < 2^32, so d*(2^32-1) < 2^64. Good, fits in uint64.
    // Result might be negative, so add p if needed.
    
    r = lo;
    uint64_t c = hi >> 32;
    uint64_t d = hi & 0xFFFFFFFFULL;
    
    uint64_t d_term = d * 0xFFFFFFFFULL;  // d * (2^32 - 1), fits in 64 bits
    
    // r = lo + d_term - c, mod p
    // Handle addition with overflow
    uint64_t sum = r + d_term;
    bool overflow = (sum < r);
    r = sum;
    
    if (overflow) {
        // sum overflowed uint64, meaning the true value is sum + 2^64
        // 2^64 ≡ 2^32 - 1 mod p
        // So add (2^32 - 1) to r
        r += 0xFFFFFFFFULL;
        if (r < 0xFFFFFFFFULL) {
            // This also overflowed, add another 2^32 - 1
            r += 0xFFFFFFFFULL;
        }
    }
    
    // Subtract c
    if (r >= c) {
        r -= c;
    } else {
        // Underflow: add MODULUS
        r = r + MODULUS - c;
    }
    
    // Final reduction: ensure r < MODULUS
    if (r >= MODULUS) {
        r -= MODULUS;
    }
    
    // One more check (belt and suspenders)
    if (r >= MODULUS) {
        r -= MODULUS;
    }
    
    return r;
}

// =============================================================================
// Arithmetic Operations
// =============================================================================

Goldilocks Goldilocks::operator+(const Goldilocks& other) const {
    uint64_t sum = value + other.value;
    // Check for overflow or >= MODULUS
    if (sum < value || sum >= MODULUS) {
        sum -= MODULUS;  // Works because MODULUS < 2^64 and sum < 2*MODULUS
    }
    Goldilocks result;
    result.value = sum;
    return result;
}

Goldilocks Goldilocks::operator-(const Goldilocks& other) const {
    Goldilocks result;
    if (value >= other.value) {
        result.value = value - other.value;
    } else {
        result.value = MODULUS - (other.value - value);
    }
    return result;
}

Goldilocks Goldilocks::operator*(const Goldilocks& other) const {
    uint64_t hi, lo;
    
#ifdef _MSC_VER
    lo = _umul128(value, other.value, &hi);
#else
    // GCC/Clang: use __uint128_t
    __uint128_t product = static_cast<__uint128_t>(value) * other.value;
    lo = static_cast<uint64_t>(product);
    hi = static_cast<uint64_t>(product >> 64);
#endif
    
    Goldilocks result;
    result.value = reduce128(lo, hi);
    return result;
}

Goldilocks& Goldilocks::operator+=(const Goldilocks& other) {
    *this = *this + other;
    return *this;
}

Goldilocks& Goldilocks::operator-=(const Goldilocks& other) {
    *this = *this - other;
    return *this;
}

Goldilocks& Goldilocks::operator*=(const Goldilocks& other) {
    *this = *this * other;
    return *this;
}

// =============================================================================
// Field Operations (STARK/FRI support)
// =============================================================================

Goldilocks Goldilocks::pow(uint64_t exp) const {
    if (exp == 0) return Goldilocks::one();
    if (is_zero()) return Goldilocks::zero();
    
    Goldilocks base = *this;
    Goldilocks result = Goldilocks::one();
    
    while (exp > 0) {
        if (exp & 1) {
            result = result * base;
        }
        base = base * base;
        exp >>= 1;
    }
    return result;
}

Goldilocks Goldilocks::inv() const {
    // Fermat's little theorem: a^(-1) = a^(p-2) mod p
    // p - 2 = 0xFFFFFFFEFFFFFFFF
    return pow(MODULUS - 2);
}

Goldilocks Goldilocks::neg() const {
    if (is_zero()) return Goldilocks::zero();
    Goldilocks result;
    result.value = MODULUS - value;
    return result;
}

Goldilocks Goldilocks::primitive_root_of_unity(uint64_t n) {
    // p - 1 = 2^64 - 2^32 = 2^32 * (2^32 - 1)
    // Maximum power-of-2 subgroup: 2^32
    // omega_n = g^((p-1)/n) where g = 7 (primitive root)
    //
    // For n = 2^k, exponent = (p-1)/n = (2^64 - 2^32) / 2^k
    //                       = 2^(32-k) * (2^32 - 1)  [for k <= 32]
    
    uint64_t exponent = (MODULUS - 1) / n;
    return Goldilocks(PRIMITIVE_ROOT).pow(exponent);
}

// =============================================================================
// Utility
// =============================================================================

std::string Goldilocks::to_string() const {
    std::ostringstream oss;
    oss << "G(" << value << ")";
    return oss.str();
}

std::ostream& operator<<(std::ostream& os, const Goldilocks& g) {
    os << g.to_string();
    return os;
}

} // namespace cortex
} // namespace glofica
