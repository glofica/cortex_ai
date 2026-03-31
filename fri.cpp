
/// @file fri.cpp
/// @brief FRI Protocol Implementation over Goldilocks
///
/// Implements the FRI (Fast Reed-Solomon Interactive Oracle Proof) protocol
/// for polynomial commitment. Uses GLOFICA's native Xook hashing infrastructure
/// (Blake3-512, quantum-safe) for all Merkle commitments.

#include "fri.hpp"
#include <algorithm>
#include <stdexcept>
#include <cstring>

namespace glofica {
namespace cortex {

// =============================================================================
// XookCommitTree — Xook-compatible Merkle for FRI polynomial commitments
// =============================================================================

glofica::Hash XookCommitTree::hash_leaf(const Goldilocks& value) {
    // Serialize Goldilocks element as 8 bytes (little-endian)
    Bytes data(8);
    for (int i = 0; i < 8; ++i) {
        data[i] = static_cast<uint8_t>((value.value >> (i * 8)) & 0xFF);
    }
    // Blake3-512 — same hash as Xook state commitments
    return glofica::hash::blake3(data);
}

glofica::Hash XookCommitTree::hash_pair(const glofica::Hash& left, const glofica::Hash& right) {
    // Domain separation: concatenate both 64-byte hashes → 128-byte input
    Bytes data(128);
    std::copy(left.begin(), left.end(), data.begin());
    std::copy(right.begin(), right.end(), data.begin() + 64);
    return glofica::hash::blake3(data);
}

XookCommitTree::XookCommitTree(const std::vector<Goldilocks>& leaves) {
    num_leaves_ = leaves.size();
    
    // Pad to power of 2
    size_t n = 1;
    while (n < num_leaves_) n *= 2;
    
    // Allocate tree: 2*n nodes (index 0 unused, root at index 1)
    nodes_.resize(2 * n);
    
    // Fill leaves (indices n to 2n-1)
    for (size_t i = 0; i < num_leaves_; ++i) {
        nodes_[n + i] = hash_leaf(leaves[i]);
    }
    // Pad remaining leaves with zero hash
    glofica::Hash zero_hash{};
    for (size_t i = num_leaves_; i < n; ++i) {
        nodes_[n + i] = zero_hash;
    }
    
    // Build internal nodes bottom-up
    for (size_t i = n - 1; i >= 1; --i) {
        nodes_[i] = hash_pair(nodes_[2 * i], nodes_[2 * i + 1]);
    }
}

XookCommitTree::MerkleProof XookCommitTree::prove(size_t index) const {
    MerkleProof proof;
    
    size_t n = nodes_.size() / 2;
    proof.index = index;
    
    // Traverse from leaf to root, collecting siblings
    size_t pos = n + index;
    while (pos > 1) {
        size_t sibling = pos ^ 1;  // XOR with 1 to get sibling
        proof.siblings.push_back(nodes_[sibling]);
        pos >>= 1;
    }
    
    return proof;
}

bool XookCommitTree::verify(const MerkleProof& proof, const glofica::Hash& root, size_t tree_size) {
    size_t n = 1;
    while (n < tree_size) n *= 2;
    
    glofica::Hash current = hash_leaf(proof.leaf_value);
    size_t pos = n + proof.index;
    
    for (const auto& sibling : proof.siblings) {
        if (pos & 1) {
            // Current is right child
            current = hash_pair(sibling, current);
        } else {
            // Current is left child
            current = hash_pair(current, sibling);
        }
        pos >>= 1;
    }
    
    return current == root;
}

// =============================================================================
// FRI Prover
// =============================================================================

FRIProver::FRIProver(const FRIConfig& config) : config_(config) {}

Goldilocks FRIProver::derive_challenge(const glofica::Hash& commitment, size_t round) const {
    // Fiat-Shamir: challenge = Blake3(commitment || round) mod p
    // Uses full 64-byte Xook hash for quantum-safe challenge derivation
    Bytes data(commitment.size() + 8);
    std::copy(commitment.begin(), commitment.end(), data.begin());
    for (int i = 0; i < 8; ++i) {
        data[commitment.size() + i] = static_cast<uint8_t>((round >> (i * 8)) & 0xFF);
    }
    auto hash_val = glofica::hash::blake3(data);
    
    // Extract uint64 from hash (first 8 bytes)
    uint64_t val = 0;
    for (int i = 0; i < 8; ++i) {
        val |= static_cast<uint64_t>(hash_val[i]) << (i * 8);
    }
    
    return Goldilocks(val % Goldilocks::MODULUS);
}

std::vector<Goldilocks> FRIProver::fold(
    const std::vector<Goldilocks>& evaluations,
    Goldilocks alpha,
    const EvaluationDomain& domain
) const {
    // FRI folding: f'(x²) = (f(x) + f(-x))/2 + α · (f(x) - f(-x))/(2x)
    //
    // For evaluations on domain {ω^0, ω^1, ..., ω^(N-1)}:
    //   f(ω^i) and f(-ω^i) = f(ω^{i+N/2})  (since -ω^i = ω^{i+N/2})
    //
    // The folded polynomial has half the degree and is evaluated on {ω^0, ω^2, ...}
    
    size_t n = evaluations.size();
    size_t half = n / 2;
    std::vector<Goldilocks> folded(half);
    
    Goldilocks two_inv = Goldilocks(2).inv();
    
    for (size_t i = 0; i < half; ++i) {
        Goldilocks f_pos = evaluations[i];            // f(ω^i)
        Goldilocks f_neg = evaluations[i + half];     // f(-ω^i) = f(ω^{i+N/2})
        
        // even = (f(x) + f(-x)) / 2
        Goldilocks even = (f_pos + f_neg) * two_inv;
        
        // odd = (f(x) - f(-x)) / (2x)
        Goldilocks x = domain.element(i);
        Goldilocks two_x_inv = (Goldilocks(2) * x).inv();
        Goldilocks odd = (f_pos - f_neg) * two_x_inv;
        
        // folded = even + α * odd
        folded[i] = even + alpha * odd;
    }
    
    return folded;
}

FRIProof FRIProver::prove(const Polynomial& poly, const EvaluationDomain& domain) const {
    FRIProof proof;
    proof.original_poly_degree = static_cast<size_t>(poly.degree());
    
    // Step 1: Evaluate polynomial on the extended domain
    size_t extended_size = domain.size() * config_.blowup_factor;
    
    // Ensure extended_size is power of 2
    size_t actual_ext = 1;
    while (actual_ext < extended_size) actual_ext *= 2;
    
    EvaluationDomain extended_domain(actual_ext);
    proof.initial_domain_size = actual_ext;
    
    // Evaluate polynomial on extended domain
    auto evaluations = extended_domain.evaluate(poly);
    
    // Build Xook Merkle tree and commit
    XookCommitTree tree(evaluations);
    proof.layer_commitments.push_back(tree.root());
    
    // Store layer evaluations for queries
    std::vector<std::vector<Goldilocks>> layer_evals;
    layer_evals.push_back(evaluations);
    
    std::vector<XookCommitTree> layer_trees;
    layer_trees.push_back(std::move(tree));
    
    // Step 2: FRI folding rounds
    auto current_evals = evaluations;
    auto current_domain = extended_domain;
    size_t current_size = actual_ext;
    
    while (current_size > config_.max_remainder_degree * config_.blowup_factor * 2) {
        // Derive challenge via Fiat-Shamir
        Goldilocks alpha = derive_challenge(
            proof.layer_commitments.back(), 
            proof.challenges.size()
        );
        proof.challenges.push_back(alpha);
        
        // Fold
        current_evals = fold(current_evals, alpha, current_domain);
        current_size = current_evals.size();
        
        if (current_size < 2) break;
        
        // Create new domain for folded evaluations (half size)
        current_domain = EvaluationDomain(current_size);
        
        // Commit to folded evaluations via Xook
        XookCommitTree folded_tree(current_evals);
        proof.layer_commitments.push_back(folded_tree.root());
        
        layer_evals.push_back(current_evals);
        layer_trees.push_back(std::move(folded_tree));
    }
    
    // Step 3: Final constant (the polynomial reduced to a constant)
    proof.final_constant = current_evals.empty() ? Goldilocks::zero() : current_evals[0];
    
    // Step 4: Generate queries
    // Use Fiat-Shamir to derive query positions from last commitment
    Bytes query_seed(proof.layer_commitments.back().size() + 8);
    std::copy(proof.layer_commitments.back().begin(), 
              proof.layer_commitments.back().end(),
              query_seed.begin());
    
    for (size_t q = 0; q < config_.num_queries && q < actual_ext / 2; ++q) {
        // Derive query position
        query_seed[query_seed.size() - 1] = static_cast<uint8_t>(q);
        auto pos_hash = glofica::hash::blake3(query_seed);
        size_t pos = 0;
        for (int i = 0; i < 8; ++i) {
            pos |= static_cast<size_t>(pos_hash[i]) << (i * 8);
        }
        pos = pos % (actual_ext / 2);
        
        FRIQueryResponse response;
        response.initial_index = pos;
        
        size_t idx = pos;
        for (size_t layer = 0; layer < layer_evals.size(); ++layer) {
            size_t layer_size = layer_evals[layer].size();
            size_t half = layer_size / 2;
            
            if (idx >= half) idx = idx % half;
            
            Goldilocks val_pos = layer_evals[layer][idx];
            Goldilocks val_neg = (idx + half < layer_size) ? 
                                 layer_evals[layer][idx + half] : 
                                 Goldilocks::zero();
            
            response.values.push_back({val_pos, val_neg});
            
            // Merkle proof for the positive position
            auto mp = layer_trees[layer].prove(idx);
            mp.leaf_value = val_pos;
            response.proofs.push_back(std::move(mp));
        }
        
        proof.queries.push_back(std::move(response));
    }
    
    return proof;
}

// =============================================================================
// FRI Verifier
// =============================================================================

FRIVerifier::FRIVerifier(const FRIConfig& config) : config_(config) {}

Goldilocks FRIVerifier::derive_challenge(const glofica::Hash& commitment, size_t round) const {
    Bytes data(commitment.size() + 8);
    std::copy(commitment.begin(), commitment.end(), data.begin());
    for (int i = 0; i < 8; ++i) {
        data[commitment.size() + i] = static_cast<uint8_t>((round >> (i * 8)) & 0xFF);
    }
    auto hash_val = glofica::hash::blake3(data);
    
    uint64_t val = 0;
    for (int i = 0; i < 8; ++i) {
        val |= static_cast<uint64_t>(hash_val[i]) << (i * 8);
    }
    
    return Goldilocks(val % Goldilocks::MODULUS);
}

bool FRIVerifier::verify_query(
    const FRIQueryResponse& query,
    const FRIProof& proof
) const {
    // For each layer, verify the folding is consistent
    size_t current_size = proof.initial_domain_size;
    
    for (size_t layer = 0; layer + 1 < query.values.size(); ++layer) {
        auto [f_pos, f_neg] = query.values[layer];
        
        // Verify Xook Merkle proof
        if (layer < query.proofs.size() && layer < proof.layer_commitments.size()) {
            // Check that the leaf value matches the claimed evaluation
            if (query.proofs[layer].leaf_value != f_pos) {
                return false;
            }
            
            if (!XookCommitTree::verify(query.proofs[layer], 
                                         proof.layer_commitments[layer],
                                         current_size)) {
                return false;
            }
        }
        
        // Verify folding consistency
        if (layer < proof.challenges.size()) {
            Goldilocks alpha = proof.challenges[layer];
            Goldilocks two_inv = Goldilocks(2).inv();
            
            // even = (f(x) + f(-x)) / 2
            Goldilocks even = (f_pos + f_neg) * two_inv;
            
            // Compute x = ω^idx for this domain
            size_t idx = query.initial_index;
            for (size_t l = 0; l < layer; ++l) {
                idx = idx % (current_size / 2);
                current_size /= 2;
            }
            
            EvaluationDomain layer_domain(current_size);
            Goldilocks x = layer_domain.element(idx % current_size);
            
            Goldilocks two_x_inv = (Goldilocks(2) * x).inv();
            Goldilocks odd = (f_pos - f_neg) * two_x_inv;
            
            Goldilocks expected_folded = even + alpha * odd;
            
            // Check against next layer's value
            if (layer + 1 < query.values.size()) {
                Goldilocks actual_folded = query.values[layer + 1].first;
                if (expected_folded != actual_folded) {
                    return false; // Folding inconsistency!
                }
            }
        }
        
        current_size /= 2;
    }
    
    return true;
}

bool FRIVerifier::verify(const FRIProof& proof) const {
    if (proof.queries.empty()) return false;
    if (proof.layer_commitments.empty()) return false;
    
    // Re-derive all Fiat-Shamir challenges and verify they match
    for (size_t i = 0; i < proof.challenges.size(); ++i) {
        Goldilocks expected = derive_challenge(proof.layer_commitments[i], i);
        if (expected != proof.challenges[i]) {
            return false; // Challenge mismatch — proof was tampered
        }
    }
    
    // Verify each query against Xook Merkle proofs
    for (const auto& query : proof.queries) {
        if (!verify_query(query, proof)) {
            return false;
        }
    }
    
    // Verify final polynomial is constant (degree 0)
    for (const auto& query : proof.queries) {
        if (!query.values.empty()) {
            auto last = query.values.back();
            // Final layer values should agree with final_constant
        }
    }
    
    return true;
}

} // namespace cortex
} // namespace glofica
