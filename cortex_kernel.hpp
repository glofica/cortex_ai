
/// @file cortex_kernel.hpp
/// @brief Cortex IA Sovereign Kernel — Intent-to-Action Verifier
/// 
/// =============================================================================
///                    CORTEX IA — AGENTIC KERNEL
/// =============================================================================
///
/// The CortexKernel replaces the traditional VM execution model.
/// Instead of executing opcodes step-by-step, it VERIFIES that an AI agent's
/// proposed action is a valid consequence of a user's intent.
///
/// Architecture:
///   1. User submits Intent (natural language + signature)
///   2. AI Agent (off-chain) thinks and produces an Action + Proof
///   3. CortexKernel (on-chain) verifies: Verify(Intent, Action, Proof) == true
///   4. If valid, state transition is applied atomically
///
/// This is the "Cadenero" (Bouncer) — it doesn't think, it VERIFIES.
///
/// Security model:
///   - Even if the AI hallucinates, the Kernel catches it
///   - Balance checks are hardcoded (can't be bypassed by proof)
///   - Slippage limits protect against market manipulation
///   - Constitutional constraints are immutable
///
/// =============================================================================

#ifndef GLOFICA_CORTEX_KERNEL_HPP
#define GLOFICA_CORTEX_KERNEL_HPP

#include "goldilocks.hpp"
#include "../ledger/types.hpp"
#include "../common/hash.hpp"
#include "stark_proof.hpp"
#include "stark_verifier.hpp"
#include <vector>
#include <string>
#include <array>
#include <cstdint>
#include <memory>

namespace glofica {

// Forward declarations
namespace ledger { class State; }

namespace cortex {

// =============================================================================
// Data Structures
// =============================================================================

/// @brief Type of agent action
enum class AgentOpType : uint8_t {
    TRANSFER = 0,   ///< Simple token transfer
    SWAP = 1,       ///< DEX/Market swap operation
    STAKE = 2,      ///< Staking/delegation
    VOTE = 3,       ///< Governance vote
    POLICY = 4,     ///< Monetary policy adjustment
    AUDIT = 5,      ///< Trigger audit/scan
    OPTIMIZE = 6,   ///< Portfolio/treasury optimization
    PREDICT = 7     ///< AI prediction with commitment
};

/// @brief User's intent — what they WANT to happen (high-level)
struct AgentIntent {
    /// Natural language goal (e.g., "Optimize my portfolio for low risk")
    std::string natural_language_goal;
    
    /// Hash of the state snapshot the agent saw when reasoning
    std::array<uint8_t, 64> context_state_hash;
    
    /// Timestamp when intent was created
    uint64_t timestamp;
    
    /// User's cryptographic signature authorizing this intent
    std::array<uint8_t, 64> user_signature;
    
    /// User's address (signer)
    ledger::Address signer;
    
    /// Maximum gas/fee the user is willing to pay
    uint64_t max_fee;
};

/// @brief Agent's proposed action — what the AI DECIDED to do
struct AgentAction {
    /// Type of operation
    AgentOpType op_type;
    
    /// Source address (usually == intent.signer)
    ledger::Address source;
    
    /// Target address for the operation
    ledger::Address target;
    
    /// Amount to transfer/stake/swap
    uint64_t amount;
    
    /// Maximum acceptable slippage in basis points (e.g., 100 = 1%)
    uint64_t max_slippage_bps;
    
    /// Additional parameters (operation-specific)
    std::vector<uint64_t> params;
};

/// @brief Execution trace — the provable record of what the agent computed
struct ExecutionTrace {
    /// The balance changes this action would produce (the "Write Set")
    /// Each pair is (address, delta): positive = credit, interpreted as uint64
    std::vector<std::pair<ledger::Address, int64_t>> balance_deltas;
    
    /// Hash of the proof-of-thought (commitment to the reasoning process)
    /// LEGACY (v1): Blake3 hash of (intent + action + model_hash)
    /// STARK  (v2): Hash derived from StarkProof verification
    std::array<uint8_t, 64> proof_hash;
    
    /// Hash of the AI model that generated this trace
    /// (The "Constitution" — ensures the same model was used)
    std::array<uint8_t, 64> model_commitment;
    
    /// Confidence score from the neural network (quantized to Goldilocks)
    Goldilocks confidence_score;
    
    /// STARK proof of correct neural inference (v2+)
    /// When present, the kernel uses cryptographic STARK verification
    /// instead of the legacy Blake3 hash check.
    /// nullptr = legacy mode (Blake3 only)
    std::shared_ptr<StarkProof> stark_proof;
};

/// @brief Result of Cortex verification
struct CortexVerifyResult {
    bool accepted;                  ///< Whether the transaction was accepted
    std::string reason;             ///< Human-readable reason (for logs)
    uint64_t gas_equivalent;        ///< Computational cost equivalent
};


// =============================================================================
// The Sovereign Kernel (The Verifier)
// =============================================================================

/// @brief The Cortex Agentic Kernel — verifies AI agent outputs
///
/// This class is the REPLACEMENT for the traditional VM execution model.
/// Instead of running bytecode, it verifies mathematical proofs of inference.
///
/// Verification pipeline:
///   1. Validate user signature on the intent
///   2. Verify proof hash commitment (proof-of-thought) 
///   3. Check hard constraints (balance, slippage, constitutional rules)
///   4. If all pass, apply state transitions
class CortexKernel {
public:
    CortexKernel() = default;
    
    /// @brief Primary verification function
    /// @param intent What the user wanted
    /// @param action What the AI agent decided
    /// @param trace The provable execution record
    /// @param state The current ledger state (for balance checks)
    /// @return Verification result
    CortexVerifyResult verify_transaction(
        const AgentIntent& intent,
        const AgentAction& action,
        const ExecutionTrace& trace,
        ledger::State* state
    ) const;
    
    /// @brief Quick proof-only verification (no state access)
    /// Useful for pre-filtering in the mempool
    bool verify_proof_commitment(
        const AgentIntent& intent,
        const AgentAction& action,
        const ExecutionTrace& trace
    ) const;
    
    /// @brief Apply verified state changes to the ledger
    /// @pre verify_transaction() MUST have returned accepted=true
    static bool apply_state_changes(
        const ExecutionTrace& trace,
        ledger::State* state
    );
    
    // =========================================================================
    // Constitutional Constraints (Hard Rules)
    // =========================================================================
    
    /// @brief Maximum single transfer amount (anti-whale)
    static constexpr uint64_t MAX_SINGLE_TRANSFER = 1'000'000'000'000ULL; // 1 Trillion
    
    /// @brief Maximum slippage allowed (10% = 1000 bps)
    static constexpr uint64_t MAX_SLIPPAGE_BPS = 1000;
    
    /// @brief Minimum confidence score required (0.5 scaled to Goldilocks)
    static constexpr uint64_t MIN_CONFIDENCE = 32768; // 0.5 * SCALE_FACTOR
    
    /// @brief Verify hard balance constraints  
    bool verify_balance_constraints(
        const AgentAction& action,
        const ExecutionTrace& trace,
        ledger::State* state
    ) const;
    
    /// @brief Verify constitutional rules (immutable safety checks)
    bool verify_constitutional_rules(
        const AgentAction& action,
        const ExecutionTrace& trace
    ) const;
    
private:
    /// @brief STARK verifier instance (on-chain verification engine)
    StarkVerifier stark_verifier_;
    
    /// @brief Verify using STARK proof (v2 — cryptographic, unforgeable)
    /// Returns: {valid, gas_used}
    std::pair<bool, uint64_t> verify_stark_proof(
        const StarkProof& proof,
        const ExecutionTrace& trace
    ) const;
    
    /// @brief Legacy: Compute expected Blake3 proof hash (v1 fallback)
    std::array<uint8_t, 64> compute_expected_proof(
        const AgentIntent& intent,
        const AgentAction& action,
        const std::array<uint8_t, 64>& model_commitment
    ) const;
};

} // namespace cortex
} // namespace glofica

#endif // GLOFICA_CORTEX_KERNEL_HPP
