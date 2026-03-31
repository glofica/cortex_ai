
/// @file stark_proof.hpp
/// @brief STARK Proof Structures & AIR for Neural Inference Verification
///
/// =============================================================================
///                    CORTEX IA — STARK PROOF OF INFERENCE
/// =============================================================================
///
/// This file defines:
///   1. The AIR (Algebraic Intermediate Representation) for neural operations
///   2. The execution trace format for neural inference
///   3. The STARK proof structure that bundles FRI commitments
///
/// The AIR defines transition constraints for:
///   - MatMul: accum' = accum + w * x
///   - BiasAdd: output = accum + bias
///   - SquareActivation: output = input²
///
/// Security model:
///   The STARK proof cryptographically guarantees that the AI model
///   executed the correct computation on the claimed inputs.
///   A malicious agent CANNOT forge a valid proof without running
///   the actual model — this closes the Blake3 commitment gap.
///
/// =============================================================================

#ifndef GLOFICA_CORTEX_STARK_PROOF_HPP
#define GLOFICA_CORTEX_STARK_PROOF_HPP

#include "goldilocks.hpp"
#include "fri.hpp"
#include <vector>
#include <array>
#include <string>

namespace glofica {
namespace cortex {

// =============================================================================
// Execution Trace (What the AI agent computed, step by step)
// =============================================================================

/// @brief Type of operation in each trace row
enum class TraceOpType : uint8_t {
    MAC = 0,           ///< Multiply-accumulate: accum += w * x
    BIAS_ADD = 1,      ///< Bias addition: output = accum + bias
    SQUARE_ACT = 2,    ///< Square activation: output = input²
    IDENTITY = 3,      ///< Identity (copy): output = input
    BOUNDARY = 4       ///< Boundary constraint marker (first/last row)
};

/// @brief One row of the execution trace
/// Each row represents one atomic operation in the neural inference
struct TraceRow {
    Goldilocks op_type;      ///< TraceOpType encoded as field element
    Goldilocks accumulator;  ///< Current accumulation value
    Goldilocks input_val;    ///< Input being processed (x_j or bias)
    Goldilocks weight_val;   ///< Weight being used (W_ij)
    Goldilocks output_val;   ///< Result of this step
    Goldilocks step_index;   ///< Step counter (for boundary constraints)
    
    /// @brief Number of columns in the trace
    static constexpr size_t NUM_COLUMNS = 6;
    
    /// @brief Access column by index
    Goldilocks column(size_t idx) const;
    
    /// @brief Get all columns as a vector
    std::vector<Goldilocks> to_vector() const;
};

/// @brief Complete execution trace for a neural inference
struct InferenceTrace {
    std::vector<TraceRow> rows;
    
    /// @brief Public input hash (the input the model received)
    glofica::Hash input_hash;
    
    /// @brief Public output hash (the output the model produced)
    glofica::Hash output_hash;
    
    /// @brief Model weight commitment (hash of quantized weights)
    glofica::Hash model_commitment;
    
    /// @brief Number of rows (must be power of 2 — pad if needed)
    size_t padded_size() const;
    
    /// @brief Pad trace to next power of 2
    void pad_to_power_of_2();
    
    /// @brief Extract column i from all rows
    std::vector<Goldilocks> column(size_t col_idx) const;
};

// =============================================================================
// AIR Constraints (What MUST be true between consecutive rows)
// =============================================================================

/// @brief AIR constraint for the neural inference circuit
///
/// Transition constraints (between row[i] and row[i+1]):
///
///   1. MAC constraint:
///      If op_type == MAC: accum[i+1] = accum[i] + weight[i] * input[i]
///
///   2. BIAS_ADD constraint:
///      If op_type == BIAS_ADD: output[i] = accum[i] + input[i]
///
///   3. SQUARE_ACT constraint:
///      If op_type == SQUARE_ACT: output[i] = input[i] * input[i]
///
/// Boundary constraints:
///   - First row: accum = 0 (accumulator starts at zero)
///   - Last row: output = final_result (matches claimed output)
///
class NeuralAIR {
public:
    /// @brief Number of transition constraints
    static constexpr size_t NUM_CONSTRAINTS = 3;
    
    /// @brief Evaluate transition constraint at a point
    /// Returns the constraint polynomial value (should be 0 on valid trace)
    ///
    /// @param current Current trace row
    /// @param next Next trace row
    /// @param constraint_idx Which constraint to evaluate (0=MAC, 1=BIAS, 2=SQUARE)
    /// @return Constraint value (0 if satisfied)
    static Goldilocks evaluate_transition(
        const TraceRow& current,
        const TraceRow& next,
        size_t constraint_idx
    );
    
    /// @brief Check all transition constraints between two rows
    /// @return true if all constraints are satisfied
    static bool check_transition(const TraceRow& current, const TraceRow& next);
    
    /// @brief Check boundary constraints
    /// @return true if first/last row constraints are satisfied
    static bool check_boundary(
        const InferenceTrace& trace,
        Goldilocks expected_output
    );
    
    /// @brief Verify entire trace locally (for debugging — not used in STARK)
    static bool verify_trace(const InferenceTrace& trace);
};

// =============================================================================
// STARK Proof (Bundles everything together)
// =============================================================================

/// @brief Complete STARK proof for neural inference verification
struct StarkProof {
    /// FRI proofs for each trace column polynomial
    std::vector<FRIProof> column_proofs;
    
    /// FRI proofs for constraint composition polynomial
    FRIProof composition_proof;
    
    /// Public inputs
    glofica::Hash input_hash;
    glofica::Hash output_hash;
    glofica::Hash model_commitment;
    
    /// The claimed output value (what the AI decided)
    Goldilocks claimed_output;
    
    /// Trace metadata
    size_t trace_length;
    size_t num_columns;
    
    /// @brief Estimated proof size in bytes
    size_t estimated_size() const;
};

/// @brief STARK configuration
struct StarkConfig {
    FRIConfig fri_config;
    size_t num_constraint_composition_queries = 16;
    size_t trace_blowup = 4;  ///< Trace domain extension factor
};

} // namespace cortex
} // namespace glofica

#endif // GLOFICA_CORTEX_STARK_PROOF_HPP
