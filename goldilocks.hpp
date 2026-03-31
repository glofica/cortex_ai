
/// @file goldilocks.hpp
/// @brief Goldilocks Prime Field Arithmetic for Cortex IA (zkML Foundation)
/// 
/// =============================================================================
///                    CORTEX IA — GOLDILOCKS FIELD
/// =============================================================================
///
/// The Goldilocks prime p = 2^64 - 2^32 + 1 (0xFFFFFFFF00000001)
/// enables efficient modular arithmetic using only native 64-bit CPU
/// operations. This is the mathematical backbone of the Cortex zkML engine.
///
/// Key properties:
///   - All operations fit in 64-bit registers (no BigInt needed)
///   - Reduction uses shifts/adds only (no division)
///   - Compatible with NTT/FFT for polynomial operations
///   - Used by Polygon, Plonky2, and other production ZK systems
///
/// Paper reference: Equation (1) and (2) from GLOFICA whitepaper
///   p = 2^64 - 2^32 + 1
///   z ≡ z_lo - z_hi + z_hi · 2^32 (mod p)
///
/// =============================================================================

#ifndef GLOFICA_CORTEX_GOLDILOCKS_HPP
#define GLOFICA_CORTEX_GOLDILOCKS_HPP

#include <cstdint>
#include <iostream>
#include <string>

namespace glofica {
namespace cortex {

/// @brief Element of the Goldilocks prime field F_p where p = 2^64 - 2^32 + 1
struct Goldilocks {
    /// The Goldilocks prime modulus
    static constexpr uint64_t MODULUS = 0xFFFFFFFF00000001ULL;
    
    /// Primitive root of the multiplicative group (used for NTT)
    static constexpr uint64_t PRIMITIVE_ROOT = 7ULL;
    
    /// The canonical value in [0, MODULUS)
    uint64_t value;
    
    /// @brief Default constructor (zero element)
    constexpr Goldilocks() : value(0) {}
    
    /// @brief Construct from raw uint64 (auto-reduced if >= MODULUS)
    constexpr explicit Goldilocks(uint64_t v) : value(v >= MODULUS ? v - MODULUS : v) {}
    
    /// @brief Zero element (additive identity)
    static constexpr Goldilocks zero() { return Goldilocks(0); }
    
    /// @brief One element (multiplicative identity)
    static constexpr Goldilocks one() { return Goldilocks(1); }
    
    // =========================================================================
    // Arithmetic Operators (all mod p)
    // =========================================================================
    
    /// @brief Modular addition
    Goldilocks operator+(const Goldilocks& other) const;
    
    /// @brief Modular subtraction
    Goldilocks operator-(const Goldilocks& other) const;
    
    /// @brief Modular multiplication (uses 128-bit intermediate)
    Goldilocks operator*(const Goldilocks& other) const;
    
    /// @brief Compound assignment operators
    Goldilocks& operator+=(const Goldilocks& other);
    Goldilocks& operator-=(const Goldilocks& other);
    Goldilocks& operator*=(const Goldilocks& other);
    
    // =========================================================================
    // Comparison Operators
    // =========================================================================
    
    bool operator==(const Goldilocks& other) const { return value == other.value; }
    bool operator!=(const Goldilocks& other) const { return value != other.value; }
    bool operator<(const Goldilocks& other) const { return value < other.value; }
    
    // =========================================================================
    // Utility
    // =========================================================================
    
    /// @brief Check if this is the zero element
    bool is_zero() const { return value == 0; }
    
    /// @brief Check if this is the one element
    bool is_one() const { return value == 1; }
    
    // =========================================================================
    // Field Operations (required for STARK/FRI)
    // =========================================================================
    
    /// @brief Modular exponentiation: this^exp mod p
    /// Uses square-and-multiply (O(log exp))
    Goldilocks pow(uint64_t exp) const;
    
    /// @brief Modular inverse: this^(-1) mod p
    /// Uses Fermat's little theorem: a^(-1) = a^(p-2) mod p
    /// @pre !is_zero() (zero has no inverse)
    Goldilocks inv() const;
    
    /// @brief Additive negation: -this mod p
    Goldilocks neg() const;
    
    /// @brief Get primitive N-th root of unity (N must be power of 2, N <= 2^32)
    /// ω_N = g^((p-1)/N) where g is the primitive root
    static Goldilocks primitive_root_of_unity(uint64_t n);
    
    /// @brief String representation for debugging
    std::string to_string() const;
    
    /// @brief Stream output
    friend std::ostream& operator<<(std::ostream& os, const Goldilocks& g);
    
    /// @brief Reduce a 128-bit product to a Goldilocks element
    /// Uses the identity: 2^64 ≡ 2^32 - 1 (mod p)
    /// (public for polynomial/FRI access)
    static uint64_t reduce128(uint64_t lo, uint64_t hi);
};

} // namespace cortex
} // namespace glofica

#endif // GLOFICA_CORTEX_GOLDILOCKS_HPP
