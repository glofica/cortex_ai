
/// @file quantizer.hpp
/// @brief Deterministic Quantization: Float → Goldilocks Field Element
/// 
/// =============================================================================
///                    CORTEX IA — QUANTIZER
/// =============================================================================
///
/// Bridges the probabilistic world of neural networks (float weights) to
/// the deterministic world of the Goldilocks field (uint64 elements).
///
/// Uses fixed-point arithmetic with scaling factor S = 2^16 = 65536.
/// This gives 4 decimal digits of fractional precision while keeping
/// all operations within 64-bit native CPU registers.
///
/// Paper reference: Equation (3)
///   Q(w) = ⌊w · S + 0.5⌋ mod p
///
/// Negative values use two's complement in the field:
///   -x ≡ p - x
///
/// =============================================================================

#ifndef GLOFICA_CORTEX_QUANTIZER_HPP
#define GLOFICA_CORTEX_QUANTIZER_HPP

#include "goldilocks.hpp"
#include <vector>
#include <span>
#include <cmath>

namespace glofica {
namespace cortex {

/// @brief Scaling factor for fixed-point quantization (2^16)
constexpr uint64_t SCALE_FACTOR = 65536ULL;
constexpr float SCALE_FACTOR_F = 65536.0f;

/// @brief Deterministic quantizer: maps real numbers to Goldilocks field elements
class Quantizer {
public:
    /// @brief Quantize a single floating-point weight to a field element
    /// @param w The real-valued weight from an AI model
    /// @return The quantized field element Q(w)
    static Goldilocks quantize(float w);
    
    /// @brief Quantize a double-precision weight
    static Goldilocks quantize(double w);
    
    /// @brief Quantize an entire layer of weights (vectorized)
    /// @param weights Span of float weights from a neural network layer
    /// @return Vector of quantized field elements
    static std::vector<Goldilocks> quantize_layer(std::span<const float> weights);
    
    /// @brief Quantize an entire layer of double weights
    static std::vector<Goldilocks> quantize_layer(std::span<const double> weights);
    
    /// @brief Dequantize a field element back to float (for debugging only!)
    /// @note This is lossy and should NEVER be used in consensus-critical paths
    static float dequantize(const Goldilocks& g);
    
    /// @brief Get the scaling factor
    static constexpr uint64_t scale_factor() { return SCALE_FACTOR; }
};

} // namespace cortex
} // namespace glofica

#endif // GLOFICA_CORTEX_QUANTIZER_HPP
