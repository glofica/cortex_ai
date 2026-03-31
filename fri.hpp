
/// @file fri.hpp
/// @brief FRI (Fast Reed-Solomon Interactive Oracle Proof) over Goldilocks
///
/// =============================================================================
///                    CORTEX IA — FRI COMMITMENT SCHEME
/// =============================================================================
///
/// FRI proves that a committed polynomial has bounded degree.
/// This is the core cryptographic primitive used by STARK to verify
/// that the AI agent's execution trace is valid.
///
/// Protocol summary:
///   1. COMMIT: Evaluate poly on extended domain, build Xook Merkle tree
///   2. FOLD:   For log(N) rounds, halve the degree using verifier challenges
///   3. QUERY:  Verifier picks random positions, prover reveals + Merkle paths
///   4. VERIFY: Check folding consistency and Merkle proofs
///
/// Security:
///   Uses GLOFICA's native Blake3-512 (quantum-safe) via glofica::Hash.
///   All hashing is consistent with the Xook Merkle Tree infrastructure.
///
/// =============================================================================

#ifndef GLOFICA_CORTEX_FRI_HPP
#define GLOFICA_CORTEX_FRI_HPP

#include "goldilocks.hpp"
#include "polynomial.hpp"
#include "../common/hash.hpp"
#include <vector>
#include <cstdint>

namespace glofica {
namespace cortex {

// =============================================================================
// Xook-Compatible Merkle Commitment (FRI polynomial commitment layer)
// =============================================================================

/// @brief Merkle tree for committing to polynomial evaluations
///
/// Uses glofica::Hash (Blake3-512, 64 bytes) — the same quantum-safe
/// hash primitive used by the Xook Jellyfish Merkle Tree throughout
/// the GLOFICA protocol. This ensures cryptographic consistency:
///   - Same hash function as state commitments
///   - Same quantum resistance level (512-bit)
///   - Same Blake3 implementation (no duplicate code)
///
/// NOTE: This is an EPHEMERAL commitment tree used only during FRI
/// proof generation/verification. It differs from Xook's persistent
/// Jellyfish Merkle Tree, which manages versioned blockchain state.
/// FRI needs a simple binary Merkle tree for polynomial evaluations,
/// while Xook provides a 16-ary radix tree for key-value state.
class XookCommitTree {
public:
    /// @brief Build tree from leaf values (Goldilocks field elements)
    explicit XookCommitTree(const std::vector<Goldilocks>& leaves);
    
    /// @brief Get the Merkle root (Blake3-512, quantum-safe)
    const glofica::Hash& root() const { return nodes_[1]; }
    
    /// @brief Merkle authentication path for a single leaf
    struct MerkleProof {
        Goldilocks leaf_value;
        std::vector<glofica::Hash> siblings;  ///< Sibling hashes along path to root
        size_t index;
    };
    
    /// @brief Generate authentication path for leaf at index
    MerkleProof prove(size_t index) const;
    
    /// @brief Verify a Merkle proof against a known root
    static bool verify(const MerkleProof& proof, const glofica::Hash& root, size_t tree_size);
    
    size_t size() const { return num_leaves_; }

private:
    std::vector<glofica::Hash> nodes_;  ///< Binary tree stored as array (index 1 = root)
    size_t num_leaves_;
    
    /// @brief Hash a Goldilocks element (8 bytes → 64-byte Blake3-512)
    static glofica::Hash hash_leaf(const Goldilocks& value);
    
    /// @brief Hash two child nodes (128 bytes → 64-byte Blake3-512)
    static glofica::Hash hash_pair(const glofica::Hash& left, const glofica::Hash& right);
};

// =============================================================================
// FRI Configuration
// =============================================================================

struct FRIConfig {
    size_t blowup_factor = 8;     ///< Domain extension factor (security)
    size_t num_queries = 30;      ///< Number of query positions (security)
    size_t folding_factor = 2;    ///< Degree reduction per round (always 2)
    size_t max_remainder_degree = 1; ///< Stop folding when degree <= this
};

// =============================================================================
// FRI Proof Structure
// =============================================================================

/// @brief One layer of FRI folding
struct FRILayer {
    glofica::Hash commitment;              ///< Xook Merkle root of this layer
    std::vector<Goldilocks> evaluations;   ///< Full evaluations (for prover)
};

/// @brief Query response for one position across all FRI layers
struct FRIQueryResponse {
    size_t initial_index;
    std::vector<std::pair<Goldilocks, Goldilocks>> values;  ///< (f(x), f(-x)) at each layer
    std::vector<XookCommitTree::MerkleProof> proofs;        ///< Merkle proofs
};

/// @brief Complete FRI proof
struct FRIProof {
    std::vector<glofica::Hash> layer_commitments;     ///< Xook Merkle roots per layer
    std::vector<Goldilocks> challenges;               ///< Verifier challenges (Fiat-Shamir)
    std::vector<FRIQueryResponse> queries;             ///< Query responses
    Goldilocks final_constant;                         ///< Final constant polynomial value
    size_t initial_domain_size;                        ///< Original domain size
    size_t original_poly_degree;                       ///< Claimed polynomial degree
};

// =============================================================================
// FRI Prover (Off-chain — generates proof)
// =============================================================================

/// @brief FRI Prover: commits to a polynomial and generates proof of degree bound
class FRIProver {
public:
    explicit FRIProver(const FRIConfig& config = FRIConfig());
    
    /// @brief Commit to a polynomial and generate FRI proof
    /// @param poly The polynomial to commit (must have degree < domain_size / blowup)
    /// @param domain The evaluation domain  
    /// @return FRI proof that the polynomial has the claimed degree bound
    FRIProof prove(const Polynomial& poly, const EvaluationDomain& domain) const;

private:
    FRIConfig config_;
    
    /// @brief Fiat-Shamir challenge from Xook commitment
    Goldilocks derive_challenge(const glofica::Hash& commitment, size_t round) const;
    
    /// @brief FRI folding: reduce polynomial degree by half
    /// f'(x²) = (f(x) + f(-x))/2 + α · (f(x) - f(-x))/(2x)
    std::vector<Goldilocks> fold(
        const std::vector<Goldilocks>& evaluations,
        Goldilocks alpha,
        const EvaluationDomain& domain
    ) const;
};

// =============================================================================
// FRI Verifier (On-chain — verifies proof)
// =============================================================================

/// @brief FRI Verifier: checks that a committed polynomial has bounded degree
/// This runs ON-CHAIN and must be efficient
class FRIVerifier {
public:
    explicit FRIVerifier(const FRIConfig& config = FRIConfig());
    
    /// @brief Verify a FRI proof
    /// @param proof The FRI proof to verify
    /// @return true if the polynomial commitment is valid
    bool verify(const FRIProof& proof) const;

private:
    FRIConfig config_;
    
    /// @brief Re-derive Fiat-Shamir challenge
    Goldilocks derive_challenge(const glofica::Hash& commitment, size_t round) const;
    
    /// @brief Verify folding consistency at one query position
    bool verify_query(
        const FRIQueryResponse& query,
        const FRIProof& proof
    ) const;
};

} // namespace cortex
} // namespace glofica

#endif // GLOFICA_CORTEX_FRI_HPP
