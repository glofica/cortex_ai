
/// @file neural_layer.cpp
/// @brief ZK-Friendly Neural Layer Implementation
///
/// All matrix operations are performed in the Goldilocks field.
/// The forward pass is a standard MatMul + Bias, but every operation
/// is modular arithmetic (mod p = 2^64 - 2^32 + 1).

#include "neural_layer.hpp"
#include <cassert>
#include <stdexcept>
#include <iostream>

namespace glofica {
namespace cortex {

// =============================================================================
// DenseLayerZK
// =============================================================================

void DenseLayerZK::load_weights(std::span<const float> weights,
                                 std::span<const float> biases,
                                 size_t input_dim, size_t output_dim) {
    if (weights.size() != input_dim * output_dim) {
        throw std::runtime_error("[Cortex] Weight matrix size mismatch: expected " + 
            std::to_string(input_dim * output_dim) + " got " + std::to_string(weights.size()));
    }
    if (biases.size() != output_dim) {
        throw std::runtime_error("[Cortex] Bias vector size mismatch");
    }
    
    input_dim_ = input_dim;
    output_dim_ = output_dim;
    
    // Quantize weight matrix (row-major: W[row][col])
    weight_matrix_.resize(output_dim);
    for (size_t row = 0; row < output_dim; ++row) {
        weight_matrix_[row].resize(input_dim);
        for (size_t col = 0; col < input_dim; ++col) {
            weight_matrix_[row][col] = Quantizer::quantize(weights[row * input_dim + col]);
        }
    }
    
    // Quantize bias vector
    biases_.resize(output_dim);
    for (size_t i = 0; i < output_dim; ++i) {
        biases_[i] = Quantizer::quantize(biases[i]);
    }
}

void DenseLayerZK::load_quantized(std::vector<std::vector<Goldilocks>>&& weight_matrix,
                                    std::vector<Goldilocks>&& biases) {
    weight_matrix_ = std::move(weight_matrix);
    biases_ = std::move(biases);
    output_dim_ = weight_matrix_.size();
    input_dim_ = weight_matrix_.empty() ? 0 : weight_matrix_[0].size();
}

std::vector<Goldilocks> DenseLayerZK::forward(const std::vector<Goldilocks>& input) const {
    if (!is_loaded()) {
        throw std::runtime_error("[Cortex] Layer '" + name_ + "' not loaded");
    }
    if (input.size() != input_dim_) {
        throw std::runtime_error("[Cortex] Layer '" + name_ + "' input dimension mismatch: "
            "expected " + std::to_string(input_dim_) + " got " + std::to_string(input.size()));
    }
    
    std::vector<Goldilocks> output(output_dim_);
    
    // Paper Eq. 4: Q(y_i) = Σ_j Q(W_ij) · Q(x_j) + Q(b_i) mod p
    for (size_t i = 0; i < output_dim_; ++i) {
        Goldilocks accumulator = Goldilocks::zero();
        
        for (size_t j = 0; j < input_dim_; ++j) {
            accumulator += weight_matrix_[i][j] * input[j];
        }
        
        output[i] = accumulator + biases_[i];
    }
    
    return output;
}

Goldilocks DenseLayerZK::forward_single(const std::vector<Goldilocks>& input) const {
    auto out = forward(input);
    if (out.empty()) {
        throw std::runtime_error("[Cortex] Layer '" + name_ + "' produced empty output");
    }
    return out[0];
}

// =============================================================================
// SquareActivation
// =============================================================================

std::vector<Goldilocks> SquareActivation::apply(const std::vector<Goldilocks>& input) {
    std::vector<Goldilocks> output;
    output.reserve(input.size());
    
    for (const auto& x : input) {
        output.push_back(x * x);  // x^2 in F_p
    }
    return output;
}

Goldilocks SquareActivation::apply_single(const Goldilocks& x) {
    return x * x;
}

// =============================================================================
// CortexNetwork
// =============================================================================

void CortexNetwork::add_layer(DenseLayerZK&& layer) {
    layers_.push_back(std::move(layer));
}

std::vector<Goldilocks> CortexNetwork::forward(const std::vector<Goldilocks>& input) const {
    if (layers_.empty()) {
        throw std::runtime_error("[Cortex] Network '" + name_ + "' has no layers");
    }
    
    std::vector<Goldilocks> current = input;
    
    for (size_t i = 0; i < layers_.size(); ++i) {
        current = layers_[i].forward(current);
        
        // Apply square activation between layers (except after the last one)
        if (i < layers_.size() - 1) {
            current = SquareActivation::apply(current);
        }
    }
    
    return current;
}

} // namespace cortex
} // namespace glofica
