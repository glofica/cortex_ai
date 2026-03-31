
/// @file stark_prover.hpp
/// @brief STARK Prover for Neural Inference — Off-Chain Proof Generation
///
/// =============================================================================
///                    CORTEX IA — STARK PROVER
/// =============================================================================
///
/// The STARK Prover runs OFF-CHAIN on the AI agent's machine.
/// It generates a cryptographic proof that:
///   1. The correct AI model was executed (model commitment matches)
///   2. The execution followed the AIR constraints (MatMul, Square, Bias)
///   3. The output is the honest result of the computation
///
/// Pipeline:
///   Neural Network → Execution Trace → Column Polynomials →
///   Constraint Composition → FRI Commitment → StarkProof
///
/// =============================================================================

#ifndef GLOFICA_CORTEX_STARK_PROVER_HPP
#define GLOFICA_CORTEX_STARK_PROVER_HPP

#include "stark_proof.hpp"
#include "neural_layer.hpp"
#include "polynomial.hpp"
#include "fri.hpp"
#include <vector>

namespace glofica {
namespace cortex {

/// @brief STARK Prover: generates proof of correct neural inference
///
/// Usage:
///   StarkProver prover(config);
///   auto trace = prover.generate_trace(network, input);
///   auto proof = prover.prove(trace);
///
/// The proof can then be verified by StarkVerifier::verify()
class StarkProver {
public:
    explicit StarkProver(const StarkConfig& config = StarkConfig());
    
    /// @brief Generate execution trace from a neural network forward pass
    /// Records every MAC, bias add, and activation step
    InferenceTrace generate_trace(
        const CortexNetwork& network,
        const std::vector<Goldilocks>& input
    ) const;
    
    /// @brief Generate STARK proof from an execution trace
    /// This is the computationally expensive step (runs off-chain)
    StarkProof prove(const InferenceTrace& trace) const;
    
    /// @brief Combined: run inference and generate proof in one call
    std::pair<std::vector<Goldilocks>, StarkProof> prove_inference(
        const CortexNetwork& network,
        const std::vector<Goldilocks>& input
    ) const;

private:
    StarkConfig config_;
    
    /// @brief Interpolate trace columns into polynomials
    std::vector<Polynomial> trace_to_polynomials(
        const InferenceTrace& trace,
        const EvaluationDomain& domain
    ) const;
    
    /// @brief Compute constraint composition polynomial
    /// C(x) = Σ α_i · c_i(x) / Z(x)
    /// where c_i are individual constraint polynomials and Z is the zerofier
    Polynomial compute_composition(
        const std::vector<Polynomial>& trace_polys,
        const EvaluationDomain& domain
    ) const;
    
    /// @brief Compute model commitment hash from quantized weights
    glofica::Hash compute_model_commitment(
        const CortexNetwork& network
    ) const;
};

} // namespace cortex
} // namespace glofica

#endif // GLOFICA_CORTEX_STARK_PROVER_HPP
