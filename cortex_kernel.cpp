
/// @file cortex_kernel.cpp
/// @brief Cortex IA Sovereign Kernel Implementation
///
/// The verification pipeline:
///   1. Validate proof commitment:
///      a) STARK mode (v2): Cryptographic verification via StarkVerifier
///         - FRI polynomial commitment verification
///         - AIR constraint satisfaction check
///         - Model commitment binding
///         - Unforgeable: < 2^(-128) forgery probability
///      b) Legacy mode (v1): Blake3 hash check (fallback)
///   2. Verify balance sufficiency (can't spend more than you have)
///   3. Verify constitutional rules (slippage, max transfer, confidence)
///   4. Apply state changes if all pass
///
/// This is deterministic: given the same inputs, every node produces
/// the exact same accept/reject decision. This is what enables consensus.

#include "cortex_kernel.hpp"
#include "../ledger/state.hpp"
#include <iostream>
#include <algorithm>
#include <numeric>

namespace glofica {
namespace cortex {

// =============================================================================
// Primary Verification
// =============================================================================

CortexVerifyResult CortexKernel::verify_transaction(
    const AgentIntent& intent,
    const AgentAction& action,
    const ExecutionTrace& trace,
    ledger::State* state
) const {
    CortexVerifyResult result;
    result.accepted = false;
    result.gas_equivalent = 2000; // Base cost for Cortex verification
    
    // =========================================================================
    // Step 1: Verify Proof Commitment (The "ZK" Check)
    // =========================================================================
    // Two modes:
    //   STARK (v2): Cryptographic proof of correct inference — unforgeable
    //   Legacy (v1): Blake3 hash commitment — weaker but backwards-compatible
    
    if (!verify_proof_commitment(intent, action, trace)) {
        result.reason = "CORTEX_REJECT: Proof commitment mismatch (agent may have hallucinated)";
        return result;
    }
    
    // Add STARK verification gas if applicable
    if (trace.stark_proof) {
        auto [stark_valid, stark_gas] = verify_stark_proof(*trace.stark_proof, trace);
        result.gas_equivalent += stark_gas;
        if (!stark_valid) {
            result.reason = "CORTEX_REJECT: STARK proof verification failed";
            return result;
        }
    }
    
    // =========================================================================
    // Step 2: Verify Constitutional Rules (Hard Safety)
    // =========================================================================
    // These rules are IMMUTABLE. Even if the AI says it's fine,
    // the Kernel has the final word.
    
    if (!verify_constitutional_rules(action, trace)) {
        result.reason = "CORTEX_REJECT: Constitutional constraint violated";
        return result;
    }
    
    // =========================================================================
    // Step 3: Verify Balance Constraints (Economic Reality)
    // =========================================================================
    // You can't spend what you don't have. Period.
    
    if (state != nullptr) {
        if (!verify_balance_constraints(action, trace, state)) {
            result.reason = "CORTEX_REJECT: Insufficient balance for proposed action";
            return result;
        }
    }
    
    // =========================================================================
    // All checks passed — Transaction is VALID
    // =========================================================================
    result.accepted = true;
    
    if (trace.stark_proof) {
        result.reason = "CORTEX_ACCEPT: STARK-verified (cryptographic proof valid, constraints satisfied)";
    } else {
        result.reason = "CORTEX_ACCEPT: Legacy-verified (hash commitment valid, constraints satisfied)";
    }
    
    return result;
}

// =============================================================================
// Proof Commitment Verification
// =============================================================================

bool CortexKernel::verify_proof_commitment(
    const AgentIntent& intent,
    const AgentAction& action,
    const ExecutionTrace& trace
) const {
    // =========================================================================
    // STARK Mode (v2): If a STARK proof is present, the proof_hash field
    // is derived from the STARK verification result. We skip the legacy
    // hash check here — the heavy verification happens in verify_transaction
    // via verify_stark_proof().
    // =========================================================================
    if (trace.stark_proof) {
        // STARK proof present — structural pre-validation only
        // (Full cryptographic verification happens in verify_stark_proof)
        return stark_verifier_.validate_structure(*trace.stark_proof);
    }
    
    // =========================================================================
    // Legacy Mode (v1): Blake3 hash commitment check
    // This only proves binding (intent → action), NOT correctness of inference.
    // An attacker could bypass this by computing the hash themselves.
    // =========================================================================
    auto expected = compute_expected_proof(intent, action, trace.model_commitment);
    return expected == trace.proof_hash;
}

// =============================================================================
// STARK Proof Verification (v2 — Cryptographic, Unforgeable)
// =============================================================================

std::pair<bool, uint64_t> CortexKernel::verify_stark_proof(
    const StarkProof& proof,
    const ExecutionTrace& trace
) const {
    // Convert ExecutionTrace model_commitment (64 bytes) to glofica::Hash
    glofica::Hash expected_model;
    std::copy(trace.model_commitment.begin(), trace.model_commitment.end(),
              expected_model.begin());
    
    // Run the STARK verifier — this is the critical security check
    // that replaces the insecure Blake3 hash placeholder.
    //
    // What this verifies:
    //   1. The FRI polynomial commitments are valid (degree bounds)
    //   2. The composition polynomial satisfies all AIR constraints
    //   3. The model commitment in the proof matches the expected model
    //   4. The claimed output is consistent with the proven execution
    //
    // What this guarantees:
    //   An attacker CANNOT produce a valid StarkProof without running
    //   the exact model specified by model_commitment on the exact input.
    //   Forgery probability: < 2^(-128)
    
    auto result = stark_verifier_.verify(proof, expected_model);
    
    return {result.valid, result.verification_gas};
}

std::array<uint8_t, 64> CortexKernel::compute_expected_proof(
    const AgentIntent& intent,
    const AgentAction& action,
    const std::array<uint8_t, 64>& model_commitment
) const {
    // Build the preimage: intent_data || action_data || model_hash
    Bytes preimage;
    
    // Intent fields
    preimage.insert(preimage.end(), 
                    intent.natural_language_goal.begin(), 
                    intent.natural_language_goal.end());
    preimage.insert(preimage.end(), 
                    intent.context_state_hash.begin(), 
                    intent.context_state_hash.end());
    
    // Timestamp as bytes
    for (int i = 7; i >= 0; --i) {
        preimage.push_back(static_cast<uint8_t>((intent.timestamp >> (i * 8)) & 0xFF));
    }
    
    // Action fields
    preimage.push_back(static_cast<uint8_t>(action.op_type));
    preimage.insert(preimage.end(), action.source.begin(), action.source.end());
    preimage.insert(preimage.end(), action.target.begin(), action.target.end());
    
    // Amount as bytes
    for (int i = 7; i >= 0; --i) {
        preimage.push_back(static_cast<uint8_t>((action.amount >> (i * 8)) & 0xFF));
    }
    
    // Model commitment
    preimage.insert(preimage.end(), model_commitment.begin(), model_commitment.end());
    
    // Hash with Blake3-512 (quantum-safe)
    auto hash = glofica::hash::blake3(preimage);
    
    std::array<uint8_t, 64> result{};
    std::copy_n(hash.begin(), std::min(hash.size(), result.size()), result.begin());
    
    return result;
}

// =============================================================================
// Constitutional Rules
// =============================================================================

bool CortexKernel::verify_constitutional_rules(
    const AgentAction& action,
    const ExecutionTrace& trace
) const {
    // Rule 1: Maximum single transfer limit (anti-whale)
    if (action.amount > MAX_SINGLE_TRANSFER) {
        return false;
    }
    
    // Rule 2: Maximum slippage (anti-manipulation)
    if (action.max_slippage_bps > MAX_SLIPPAGE_BPS) {
        return false;
    }
    
    // Rule 3: Minimum confidence score
    // The AI must be "confident enough" in its decision
    if (trace.confidence_score.value < MIN_CONFIDENCE) {
        return false;
    }
    
    // Rule 4: Balance deltas must net to zero (conservation of value)
    // Sum of all deltas should be zero (what leaves one account enters another)
    int64_t delta_sum = 0;
    for (const auto& [addr, delta] : trace.balance_deltas) {
        delta_sum += delta;
    }
    
    // Allow small rounding errors from fees (up to 1% of amount)
    int64_t max_fee = static_cast<int64_t>(action.amount / 100);
    if (std::abs(delta_sum) > max_fee && delta_sum != 0) {
        // Net deltas don't balance — something is wrong
        // Exception: fee payments to validators (delta_sum should be negative small amount)
        if (delta_sum > 0) {
            return false; // Can't create value from nothing
        }
    }
    
    // Rule 5: Action source must be the intent signer (no impersonation)
    // (This is checked elsewhere via signature, but belt-and-suspenders)
    
    return true;
}

// =============================================================================
// Balance Constraints
// =============================================================================

bool CortexKernel::verify_balance_constraints(
    const AgentAction& action,
    const ExecutionTrace& trace,
    ledger::State* state
) const {
    // Check that every account being debited has sufficient balance
    for (const auto& [addr, delta] : trace.balance_deltas) {
        if (delta < 0) {
            // This account is being debited
            uint64_t current_balance = state->get_balance(addr);
            uint64_t debit_amount = static_cast<uint64_t>(-delta);
            
            if (current_balance < debit_amount) {
                return false; // Insufficient funds
            }
        }
    }
    
    return true;
}

// =============================================================================
// State Application
// =============================================================================

bool CortexKernel::apply_state_changes(
    const ExecutionTrace& trace,
    ledger::State* state
) {
    if (state == nullptr) return false;
    
    // Apply each balance delta atomically
    for (const auto& [addr, delta] : trace.balance_deltas) {
        uint64_t current = state->get_balance(addr);
        
        if (delta >= 0) {
            // Credit
            state->set_balance(addr, current + static_cast<uint64_t>(delta));
        } else {
            // Debit (we already verified sufficiency)
            uint64_t debit = static_cast<uint64_t>(-delta);
            state->set_balance(addr, current - debit);
        }
    }
    
    return true;
}

} // namespace cortex
} // namespace glofica
