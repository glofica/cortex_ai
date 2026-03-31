
/// @file neural_layer.hpp
/// @brief ZK-Friendly Neural Network Layers for Cortex IA
/// 
/// =============================================================================
///                    CORTEX IA — NEURAL LAYERS
/// =============================================================================
///
/// Implements neural network operations entirely within the Goldilocks field.
/// All computations are deterministic and bit-perfect across architectures.
///
/// Paper reference: Equation (4)
///   Q(y) = Σ Q(W_ij) · Q(x_j) + Q(b_i) mod p
///
/// Supported layers:
///   - DenseLayerZK: Fully connected layer (MatMul + Bias)
///   - SquareActivation: ZK-friendly activation (x^2, no exponentials)
///
/// =============================================================================

#ifndef GLOFICA_CORTEX_NEURAL_LAYER_HPP
#define GLOFICA_CORTEX_NEURAL_LAYER_HPP

#include "goldilocks.hpp"
#include "quantizer.hpp"
#include <vector>
#include <span>
#include <string>

namespace glofica {
namespace cortex {

/// @brief A single fully-connected (dense) layer operating in the Goldilocks field
/// 
/// Performs: y = W·x + b (all operations modular in F_p)
/// This is the fundamental building block of the Cortex neural engine.
class DenseLayerZK {
public:
    /// @brief Construct an uninitialized layer
    DenseLayerZK() = default;
    
    /// @brief Construct with a name for debugging 
    explicit DenseLayerZK(const std::string& name) : name_(name) {}
    
    /// @brief Load weights from float arrays (auto-quantized)
    /// @param weights Flat weight matrix (row-major, size = output_dim * input_dim)
    /// @param biases Bias vector (size = output_dim)
    /// @param input_dim Number of input features
    /// @param output_dim Number of output features (neurons)
    void load_weights(std::span<const float> weights, 
                      std::span<const float> biases,
                      size_t input_dim, size_t output_dim);
    
    /// @brief Load pre-quantized weights directly 
    void load_quantized(std::vector<std::vector<Goldilocks>>&& weight_matrix,
                        std::vector<Goldilocks>&& biases);
    
    /// @brief Forward pass: y = W·x + b in F_p
    /// @param input Input vector of field elements
    /// @return Output vector of field elements
    std::vector<Goldilocks> forward(const std::vector<Goldilocks>& input) const;
    
    /// @brief Single-output forward pass (for 1D output layers)
    /// @param input Input vector of field elements
    /// @return Single output field element
    Goldilocks forward_single(const std::vector<Goldilocks>& input) const;
    
    /// @brief Get layer dimensions
    size_t input_dim() const { return input_dim_; }
    size_t output_dim() const { return output_dim_; }
    
    /// @brief Get layer name
    const std::string& name() const { return name_; }
    
    /// @brief Is this layer initialized?
    bool is_loaded() const { return !weight_matrix_.empty(); }
    
    /// @brief Access individual weight W[output][input] (for STARK trace generation)
    Goldilocks get_weight(size_t output_idx, size_t input_idx) const {
        if (output_idx < weight_matrix_.size() && input_idx < weight_matrix_[output_idx].size())
            return weight_matrix_[output_idx][input_idx];
        return Goldilocks::zero();
    }
    
    /// @brief Access individual bias b[output] (for STARK trace generation)
    Goldilocks get_bias(size_t output_idx) const {
        if (output_idx < biases_.size()) return biases_[output_idx];
        return Goldilocks::zero();
    }

private:
    std::string name_ = "unnamed";
    size_t input_dim_ = 0;
    size_t output_dim_ = 0;
    
    // Weight matrix W[output_dim][input_dim] in Goldilocks field
    std::vector<std::vector<Goldilocks>> weight_matrix_;
    
    // Bias vector b[output_dim] in Goldilocks field
    std::vector<Goldilocks> biases_;
};


/// @brief ZK-friendly square activation: f(x) = x^2
/// 
/// In ZK circuits, x^2 requires only ONE multiplication constraint.
/// Compare with ReLU (comparison = expensive) or GeLU (exponential = impossible).
/// This is the standard choice for zkML systems.
class SquareActivation {
public:
    /// @brief Apply square activation element-wise
    static std::vector<Goldilocks> apply(const std::vector<Goldilocks>& input);
    
    /// @brief Apply to single element
    static Goldilocks apply_single(const Goldilocks& x);
};


/// @brief A complete Cortex neural network (stack of layers)
/// 
/// Represents the "brain" of a Sovereign AI Agent.
/// Architecture: Input -> Dense -> x^2 -> Dense -> Output
class CortexNetwork {
public:
    CortexNetwork() = default;
    explicit CortexNetwork(const std::string& name) : name_(name) {}
    
    /// @brief Add a dense layer to the network
    void add_layer(DenseLayerZK&& layer);
    
    /// @brief Run full inference (all layers + activations)
    /// @param input Quantized input vector
    /// @return Final output vector
    std::vector<Goldilocks> forward(const std::vector<Goldilocks>& input) const;
    
    /// @brief Get the number of layers
    size_t num_layers() const { return layers_.size(); }
    
    /// @brief Access layer by index (for STARK trace generation)
    const DenseLayerZK& get_layer(size_t idx) const { return layers_[idx]; }
    
    /// @brief Get network name
    const std::string& name() const { return name_; }

private:
    std::string name_ = "cortex_default";
    std::vector<DenseLayerZK> layers_;
};

} // namespace cortex
} // namespace glofica

#endif // GLOFICA_CORTEX_NEURAL_LAYER_HPP
