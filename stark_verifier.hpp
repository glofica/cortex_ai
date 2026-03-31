
/// @file stark_verifier.hpp
/// @brief STARK Verifier for Neural Inference — On-Chain Verification
///
/// =============================================================================
///                    CORTEX IA — STARK VERIFIER
/// =============================================================================
///
/// The STARK Verifier runs ON-CHAIN within the CortexKernel.
/// It verifies that a StarkProof is valid, which guarantees:
///
///   1. The AI model with commitment H was correctly executed
///   2. The execution trace satisfies all AIR constraints
///   3. The claimed output is the honest result
///
/// This REPLACES the insecure Blake3 hash check in verify_proof_commitment()
/// with cryptographic verification of polynomial commitments.
///
/// Security guarantee:
///   An attacker cannot forge a valid StarkProof without:
///   a) Running the exact model specified by model_commitment
///   b) Computing the honest forward pass on the claimed input
///   c) Producing output consistent with the model's computation
///
///   Probability of forgery: < 2^(-128) with standard parameters
///
/// =============================================================================

#ifndef GLOFICA_CORTEX_STARK_VERIFIER_HPP
#define GLOFICA_CORTEX_STARK_VERIFIER_HPP

#include "stark_proof.hpp"
#include "fri.hpp"

namespace glofica {
namespace cortex {

/// @brief STARK Verifier: cryptographically verifies neural inference proofs
///
/// This is the on-chain component. It must be efficient:
///   - Verification time: O(log²(N)) where N = trace length
///   - Memory: O(log(N) * num_queries)
///   - No access to the full trace (only the proof)
///
class StarkVerifier {
public:
    explicit StarkVerifier(const StarkConfig& config = StarkConfig());
    
    /// @brief Result of STARK verification
    struct VerifyResult {
        bool valid;                         ///< Proof is cryptographically valid
        std::string reason;                 ///< Human-readable reason
        uint64_t verification_gas;          ///< Gas consumed for verification
        glofica::Hash proof_hash;               ///< Xook-compatible hash of the verified proof
    };
    
    /// @brief Verify a STARK proof of neural inference
    ///
    /// Checks:
    ///   1. FRI proofs for all trace column polynomials
    ///   2. FRI proof for the composition polynomial
    ///   3. Constraint satisfaction at random evaluation points
    ///   4. Boundary constraints match public inputs
    ///   5. Model commitment matches expected
    ///
    /// @param proof The STARK proof to verify
    /// @param expected_model_commitment The expected AI model hash
    /// @return Verification result
    VerifyResult verify(
        const StarkProof& proof,
        const glofica::Hash& expected_model_commitment
    ) const;
    
    /// @brief Quick structural validation of proof format
    /// (Does not verify cryptographic content — used for mempool filtering)
    bool validate_structure(const StarkProof& proof) const;

private:
    StarkConfig config_;
    FRIVerifier fri_verifier_;
    
    /// @brief Verify all FRI commitments in the proof
    bool verify_fri_commitments(const StarkProof& proof) const;
    
    /// @brief Hash the proof for Xook commitment
    glofica::Hash compute_proof_hash(const StarkProof& proof) const;
};

} // namespace cortex
} // namespace glofica

#endif // GLOFICA_CORTEX_STARK_VERIFIER_HPP
