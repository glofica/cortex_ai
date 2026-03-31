
/// @file quantizer.cpp
/// @brief Deterministic Quantization Implementation
///
/// Converts floating-point neural network weights to Goldilocks field
/// elements using fixed-point arithmetic with S = 2^16 scaling.

#include "quantizer.hpp"
#include <cmath>
#include <algorithm>

namespace glofica {
namespace cortex {

Goldilocks Quantizer::quantize(float w) {
    // Paper Eq. 3: Q(w) = ⌊w · S + 0.5⌋ mod p
    // std::llround does the +0.5 and floor internally
    long long scaled = std::llround(static_cast<double>(w) * SCALE_FACTOR_F);
    
    if (scaled >= 0) {
        return Goldilocks(static_cast<uint64_t>(scaled));
    } else {
        // Negative values: two's complement in the field
        // -x ≡ p - x (mod p)
        uint64_t abs_val = static_cast<uint64_t>(-scaled);
        // MODULUS - abs_val, but handle case where abs_val > MODULUS
        if (abs_val >= Goldilocks::MODULUS) {
            abs_val %= Goldilocks::MODULUS;
        }
        if (abs_val == 0) return Goldilocks::zero();
        Goldilocks result;
        result.value = Goldilocks::MODULUS - abs_val;
        return result;
    }
}

Goldilocks Quantizer::quantize(double w) {
    long long scaled = std::llround(w * static_cast<double>(SCALE_FACTOR));
    
    if (scaled >= 0) {
        return Goldilocks(static_cast<uint64_t>(scaled));
    } else {
        uint64_t abs_val = static_cast<uint64_t>(-scaled);
        if (abs_val >= Goldilocks::MODULUS) {
            abs_val %= Goldilocks::MODULUS;
        }
        if (abs_val == 0) return Goldilocks::zero();
        Goldilocks result;
        result.value = Goldilocks::MODULUS - abs_val;
        return result;
    }
}

std::vector<Goldilocks> Quantizer::quantize_layer(std::span<const float> weights) {
    std::vector<Goldilocks> result;
    result.reserve(weights.size());
    
    for (float w : weights) {
        result.push_back(quantize(w));
    }
    return result;
}

std::vector<Goldilocks> Quantizer::quantize_layer(std::span<const double> weights) {
    std::vector<Goldilocks> result;
    result.reserve(weights.size());
    
    for (double w : weights) {
        result.push_back(quantize(w));
    }
    return result;
}

float Quantizer::dequantize(const Goldilocks& g) {
    // Check if it's a "negative" number (value > MODULUS/2)
    if (g.value > Goldilocks::MODULUS / 2) {
        // Negative: value represents -(MODULUS - g.value)
        uint64_t abs_val = Goldilocks::MODULUS - g.value;
        return -static_cast<float>(abs_val) / SCALE_FACTOR_F;
    } else {
        return static_cast<float>(g.value) / SCALE_FACTOR_F;
    }
}

} // namespace cortex
} // namespace glofica
