
/// @file reasoning_engine.cpp
/// @brief DeepSeek R1 Reasoning Engine Implementation
///
/// Parses <think>...</think> output from reasoning LLMs and converts
/// the final decision to structured AgentAction for Cortex verification.

#include "reasoning_engine.hpp"
#include <algorithm>
#include <sstream>
#include <cctype>

namespace glofica {
namespace cortex {

// =============================================================================
// Output Parsing
// =============================================================================

AgentThoughtProcess ReasoningEngine::parse_output(const std::string& raw_llm_output) {
    AgentThoughtProcess result;
    result.raw_output = raw_llm_output;
    result.parsed_successfully = false;
    
    if (raw_llm_output.empty()) {
        return result;
    }
    
    // Try to extract <think>...</think> block
    result.internal_monologue = extract_think_tags(raw_llm_output);
    
    if (!result.internal_monologue.empty()) {
        // Successfully found think tags — extract the decision after
        result.final_decision = extract_after_think(raw_llm_output);
        result.parsed_successfully = true;
    } else {
        // No think tags found — the entire output is the decision
        // (Some quantized models skip the think tags)
        result.final_decision = raw_llm_output;
        result.internal_monologue = "(No explicit reasoning provided)";
        result.parsed_successfully = true;
    }
    
    // Trim whitespace
    auto trim = [](std::string& s) {
        size_t start = s.find_first_not_of(" \t\n\r");
        size_t end = s.find_last_not_of(" \t\n\r");
        if (start == std::string::npos) { s = ""; return; }
        s = s.substr(start, end - start + 1);
    };
    
    trim(result.internal_monologue);
    trim(result.final_decision);
    
    return result;
}

std::string ReasoningEngine::extract_think_tags(const std::string& text) {
    // Find <think> opening tag (case-insensitive)
    std::string lower_text = text;
    std::transform(lower_text.begin(), lower_text.end(), lower_text.begin(), ::tolower);
    
    size_t start = lower_text.find("<think>");
    if (start == std::string::npos) return "";
    
    start += 7; // Length of "<think>"
    
    size_t end = lower_text.find("</think>", start);
    if (end == std::string::npos) {
        // Unclosed tag — take everything after <think> as the thought
        return text.substr(start);
    }
    
    return text.substr(start, end - start);
}

std::string ReasoningEngine::extract_after_think(const std::string& text) {
    std::string lower_text = text;
    std::transform(lower_text.begin(), lower_text.end(), lower_text.begin(), ::tolower);
    
    size_t end_tag = lower_text.find("</think>");
    if (end_tag == std::string::npos) return "";
    
    return text.substr(end_tag + 8); // Length of "</think>"
}

// =============================================================================
// JSON Parsing (Minimal — no external dependencies)
// =============================================================================

ParsedAction ReasoningEngine::parse_action_json(const std::string& json_decision) {
    ParsedAction result;
    result.valid = false;
    
    if (json_decision.empty()) return result;
    
    // Extract action type
    std::string action_str = extract_json_string(json_decision, "action");
    if (action_str == "TRANSFER" || action_str == "transfer") {
        result.op_type = AgentOpType::TRANSFER;
    } else if (action_str == "SWAP" || action_str == "swap") {
        result.op_type = AgentOpType::SWAP;
    } else if (action_str == "SELL" || action_str == "sell") {
        result.op_type = AgentOpType::SWAP; // SELL is a type of swap
    } else if (action_str == "BUY" || action_str == "buy") {
        result.op_type = AgentOpType::SWAP;
    } else if (action_str == "STAKE" || action_str == "stake") {
        result.op_type = AgentOpType::STAKE;
    } else if (action_str == "VOTE" || action_str == "vote") {
        result.op_type = AgentOpType::VOTE;
    } else if (action_str == "HOLD" || action_str == "hold" || action_str == "NONE" || action_str == "none") {
        result.op_type = AgentOpType::TRANSFER;
        result.amount = 0;
        result.reason = "Agent decided to hold/no action";
        result.valid = true;
        return result;
    } else if (!action_str.empty()) {
        result.op_type = AgentOpType::TRANSFER; // Default fallback
    } else {
        return result; // Can't determine action type
    }
    
    // Extract amount
    result.amount = extract_json_uint64(json_decision, "amount");
    
    // Extract target address
    result.target_address_hex = extract_json_string(json_decision, "target");
    if (result.target_address_hex.empty()) {
        result.target_address_hex = extract_json_string(json_decision, "to");
    }
    
    // Extract reason
    result.reason = extract_json_string(json_decision, "reason");
    if (result.reason.empty()) {
        result.reason = extract_json_string(json_decision, "rationale");
    }
    
    // Extract slippage
    uint64_t slippage = extract_json_uint64(json_decision, "max_slippage");
    if (slippage > 0) {
        result.max_slippage_bps = slippage;
    }
    
    result.valid = true;
    return result;
}

AgentAction ReasoningEngine::to_agent_action(
    const ParsedAction& parsed,
    const ledger::Address& source_address
) {
    AgentAction action;
    action.op_type = parsed.op_type;
    action.source = source_address;
    action.target.fill(0); // Default zero address
    action.amount = parsed.amount;
    action.max_slippage_bps = parsed.max_slippage_bps;
    
    // Parse hex target address if provided
    if (!parsed.target_address_hex.empty() && parsed.target_address_hex.size() >= 2) {
        std::string hex = parsed.target_address_hex;
        if (hex.substr(0, 2) == "0x" || hex.substr(0, 2) == "0X") {
            hex = hex.substr(2);
        }
        // Fill address from hex (up to 64 hex chars = 32 bytes)
        for (size_t i = 0; i < hex.size() && i/2 < action.target.size(); i += 2) {
            if (i + 1 < hex.size()) {
                std::string byte_str = hex.substr(i, 2);
                try {
                    action.target[i/2] = static_cast<uint8_t>(std::stoul(byte_str, nullptr, 16));
                } catch (...) {
                    // Invalid hex — leave as zero
                }
            }
        }
    }
    
    return action;
}

// =============================================================================
// System Prompt Builder
// =============================================================================

std::string ReasoningEngine::build_system_prompt(
    const std::string& agent_role,
    const std::vector<std::string>& constraints
) {
    std::ostringstream prompt;
    
    prompt << "You are a Sovereign AI Agent operating within the GLOFICA financial system.\n";
    prompt << "Role: " << agent_role << "\n\n";
    
    prompt << "CONSTITUTIONAL CONSTRAINTS (IMMUTABLE — CANNOT BE OVERRIDDEN):\n";
    for (size_t i = 0; i < constraints.size(); ++i) {
        prompt << "  " << (i + 1) << ". " << constraints[i] << "\n";
    }
    
    prompt << "\nOUTPUT FORMAT:\n";
    prompt << "1. First, reason step-by-step inside <think>...</think> tags.\n";
    prompt << "2. Then output your decision as a JSON object:\n";
    prompt << "   { \"action\": \"TRANSFER|SWAP|STAKE|VOTE|HOLD\",\n";
    prompt << "     \"amount\": <uint64>,\n";
    prompt << "     \"target\": \"0x...\",\n";
    prompt << "     \"max_slippage\": <basis_points>,\n";
    prompt << "     \"reason\": \"<brief explanation>\" }\n\n";
    prompt << "CRITICAL: If you are unsure, output {\"action\": \"HOLD\", \"amount\": 0, \"reason\": \"Insufficient confidence\"}.\n";
    prompt << "NEVER approve a transaction that would empty an account to an unknown address.\n";
    
    return prompt.str();
}

// =============================================================================
// Validation
// =============================================================================

bool ReasoningEngine::validate_reasoning(const AgentThoughtProcess& thought) {
    if (!thought.parsed_successfully) return false;
    
    // The monologue should have some substance (at least 10 chars)
    if (thought.internal_monologue.size() < 10) return false;
    
    // The decision should look like JSON (contains { and })
    if (thought.final_decision.find('{') == std::string::npos) return false;
    if (thought.final_decision.find('}') == std::string::npos) return false;
    
    return true;
}

// =============================================================================
// JSON Helpers (Minimal parser — no external deps)
// =============================================================================

std::string ReasoningEngine::extract_json_string(const std::string& json, const std::string& key) {
    // Find "key": "value" or "key":"value"
    std::string search = "\"" + key + "\"";
    size_t key_pos = json.find(search);
    if (key_pos == std::string::npos) return "";
    
    // Find the colon
    size_t colon = json.find(':', key_pos + search.size());
    if (colon == std::string::npos) return "";
    
    // Skip whitespace after colon
    size_t val_start = colon + 1;
    while (val_start < json.size() && std::isspace(json[val_start])) val_start++;
    
    if (val_start >= json.size()) return "";
    
    if (json[val_start] == '"') {
        // String value
        val_start++;
        size_t val_end = json.find('"', val_start);
        if (val_end == std::string::npos) return "";
        return json.substr(val_start, val_end - val_start);
    }
    
    // Non-quoted value (number, etc.) — read until , or }
    size_t val_end = json.find_first_of(",}", val_start);
    if (val_end == std::string::npos) val_end = json.size();
    
    std::string val = json.substr(val_start, val_end - val_start);
    // Trim
    while (!val.empty() && std::isspace(val.back())) val.pop_back();
    while (!val.empty() && std::isspace(val.front())) val.erase(val.begin());
    
    return val;
}

uint64_t ReasoningEngine::extract_json_uint64(const std::string& json, const std::string& key) {
    std::string val = extract_json_string(json, key);
    if (val.empty()) return 0;
    
    try {
        return std::stoull(val);
    } catch (...) {
        return 0;
    }
}

} // namespace cortex
} // namespace glofica
