
/// @file reasoning_engine.hpp
/// @brief DeepSeek R1 Reasoning Engine Adapter for Cortex IA
/// 
/// =============================================================================
///                    CORTEX IA — REASONING ENGINE
/// =============================================================================
///
/// Parses the output of reasoning-capable LLMs (DeepSeek R1, Qwen-CoT)
/// which produce structured output in the format:
///
///   <think>
///   [Internal monologue / chain-of-thought reasoning]
///   </think>
///   [Final decision in JSON format]
///
/// The internal monologue is PRIVATE (used as ZK witness).
/// The final decision is PUBLIC (submitted to the Cortex Kernel).
///
/// This separation enables "Proof of Thought" — proving that the
/// AI followed the rules without revealing its proprietary reasoning.
///
/// =============================================================================

#ifndef GLOFICA_CORTEX_REASONING_ENGINE_HPP
#define GLOFICA_CORTEX_REASONING_ENGINE_HPP

#include "cortex_kernel.hpp"
#include <string>
#include <optional>
#include <vector>

namespace glofica {
namespace cortex {

/// @brief Parsed output from a reasoning LLM
struct AgentThoughtProcess {
    /// The internal chain-of-thought (PRIVATE — witness only)
    /// This is what goes inside <think>...</think> tags
    std::string internal_monologue;
    
    /// The final decision (PUBLIC — submitted on-chain)
    /// Expected to be valid JSON with action parameters
    std::string final_decision;
    
    /// Whether the parsing was successful
    bool parsed_successfully = false;
    
    /// Raw output (for debugging)
    std::string raw_output;
};

/// @brief Parsed action from the JSON decision
struct ParsedAction {
    AgentOpType op_type = AgentOpType::TRANSFER;
    uint64_t amount = 0;
    std::string target_address_hex;
    uint64_t max_slippage_bps = 50; // Default 0.5%
    std::string reason;
    bool valid = false;
};


/// @brief Engine that interfaces with reasoning LLMs (DeepSeek R1)
///
/// This class:
///   1. Parses raw LLM output into thought + action
///   2. Validates the action format
///   3. Converts to AgentAction for Cortex Kernel verification
class ReasoningEngine {
public:
    ReasoningEngine() = default;
    
    /// @brief Parse raw LLM output into structured thought process
    /// @param raw_llm_output The complete text output from the LLM
    /// @return Parsed thought process with separate monologue and decision
    static AgentThoughtProcess parse_output(const std::string& raw_llm_output);
    
    /// @brief Parse the JSON decision string into an action
    /// @param json_decision The JSON string from final_decision
    /// @return Parsed action parameters
    static ParsedAction parse_action_json(const std::string& json_decision);
    
    /// @brief Convert a parsed action to a CortexKernel-compatible AgentAction
    /// @param parsed The parsed action from JSON
    /// @param source_address The user's address (from intent)
    /// @return AgentAction ready for verification
    static AgentAction to_agent_action(
        const ParsedAction& parsed,
        const ledger::Address& source_address
    );
    
    /// @brief Build a system prompt for the AI agent
    /// @param agent_role The role of the agent (e.g., "financial_advisor")
    /// @param constraints Constitutional constraints as text
    /// @return System prompt string
    static std::string build_system_prompt(
        const std::string& agent_role,
        const std::vector<std::string>& constraints
    );
    
    /// @brief Validate that a thought process is internally consistent
    /// @param thought The parsed thought process
    /// @return true if the reasoning appears valid
    static bool validate_reasoning(const AgentThoughtProcess& thought);
    
private:
    /// @brief Extract content between <think> tags
    static std::string extract_think_tags(const std::string& text);
    
    /// @brief Extract content after </think> tags
    static std::string extract_after_think(const std::string& text);
    
    /// @brief Simple JSON value extraction (key-value)
    static std::string extract_json_string(const std::string& json, const std::string& key);
    static uint64_t extract_json_uint64(const std::string& json, const std::string& key);
};

} // namespace cortex
} // namespace glofica

#endif // GLOFICA_CORTEX_REASONING_ENGINE_HPP
