# Cortex IA — Sovereign AI Kernel

> **Verifiable AI inference for decentralized systems**

Cortex IA replaces the traditional virtual machine with an **agentic verification kernel** — instead of re-executing every transaction, the network verifies cryptographic proofs that an AI agent computed correctly.

## Architecture

```
┌─────────────────────────────────────────────────────────┐
│                    Cortex IA Kernel                      │
│                                                         │
│  ┌──────────┐  ┌──────────────┐  ┌───────────────────┐  │
│  │ Reasoning│  │   Neural     │  │  STARK Prover     │  │
│  │ Engine   │──│   Layer      │──│  (Goldilocks/FRI) │  │
│  └──────────┘  └──────────────┘  └───────────────────┘  │
│       │              │                    │              │
│       ▼              ▼                    ▼              │
│  ┌──────────────────────────────────────────────────┐   │
│  │        Verification Pipeline                      │   │
│  │  1. Validate proof commitment (STARK or Blake3)   │   │
│  │  2. Check constitutional rules                    │   │
│  │  3. Verify balance constraints                    │   │
│  │  4. Apply state transitions                       │   │
│  └──────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────┘
```

## Core Components

| Component | Files | Description |
|-----------|-------|-------------|
| **Cortex Kernel** | `cortex_kernel.{hpp,cpp}` | Intent-to-Action verification pipeline — the core engine |
| **STARK Prover** | `stark_prover.{hpp,cpp}` | Generates cryptographic proofs of correct inference |
| **STARK Verifier** | `stark_verifier.{hpp,cpp}` | Verifies proofs in O(log n) — runs on every node |
| **Goldilocks Field** | `goldilocks.{hpp,cpp}` | Prime field arithmetic (p = 2⁶⁴ - 2³² + 1) — no BigInt |
| **FRI Protocol** | `fri.{hpp,cpp}` | Fast Reed-Solomon IOP for polynomial commitment |
| **Polynomials** | `polynomial.{hpp,cpp}` | Polynomial ops with NTT/FFT over Goldilocks |
| **STARK Proof** | `stark_proof.hpp` | AIR constraints + proof structures for neural inference |
| **Neural Layer** | `neural_layer.{hpp,cpp}` | Quantized neural ops (int8) with field-compatible arithmetic |
| **Quantizer** | `quantizer.{hpp,cpp}` | Float32 → Int8 quantization for zkML compatibility |
| **Reasoning Engine** | `reasoning_engine.{hpp,cpp}` | LLM output parser (DeepSeek R1 / Qwen) with Proof-of-Thought |

## How It Works

### 1. Agent Intent
A user submits a natural language goal:
```
"Optimize my portfolio for low risk with max 1% slippage"
```

### 2. AI Reasoning (Off-Chain)
A reasoning LLM (DeepSeek R1) processes the intent inside a TEE enclave:
```xml
<think>
User wants low-risk optimization. Current allocation is 80% XGO, 20% USDC.
Rebalancing to 50/50 would reduce volatility by ~40%.
Slippage at current liquidity: 0.3% — within bounds.
</think>
{"op": "SWAP", "amount": 30000, "target": "USDC", "slippage_bps": 50}
```

### 3. STARK Proof Generation
The neural inference is traced step-by-step (MatMul → BiasAdd → Activation), and a STARK proof is generated over the Goldilocks field:
- **Prover complexity:** O(n · log² n)
- **Verifier complexity:** O(log² n)
- **Proof size:** O(log² n)
- **Forgery probability:** < 2⁻¹²⁸

### 4. Kernel Verification (On-Chain)
Every node runs the lightweight STARK verifier:
```cpp
CortexVerifyResult result = kernel.verify_transaction(intent, action, trace, state);
// result.accepted = true → apply state changes
// result.reason = "CORTEX_ACCEPT: STARK-verified (cryptographic proof valid)"
```

## The Goldilocks Field

All arithmetic operates over F_p where p = 2⁶⁴ - 2³² + 1:
- Fits in native 64-bit CPU registers
- Reduction uses shifts + adds only (no division)
- Compatible with NTT for O(n log n) polynomial multiplication
- Same field used by Polygon Plonky2 and other production ZK systems

## Constitutional Rules

The kernel enforces immutable safety constraints that **override AI decisions**:

| Rule | Constraint | Purpose |
|------|------------|---------|
| Anti-whale | `amount ≤ MAX_SINGLE_TRANSFER` | Prevents market manipulation |
| Slippage cap | `slippage ≤ 500 bps (5%)` | Protects users from MEV |
| Confidence floor | `confidence ≥ 0.7` | Rejects uncertain AI outputs |
| Conservation | `Σ deltas ≈ 0` | No value created from nothing |

## Building

```bash
# Requirements: C++20 compiler (GCC 12+, Clang 15+, MSVC 2022+)
# Part of the GLOFICA DLT — builds with the full node

mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make cortex_tests
```

## Project Structure

```
cortex/
├── cortex_kernel.hpp/cpp      # Sovereign verification kernel
├── stark_prover.hpp/cpp       # STARK proof generation
├── stark_verifier.hpp/cpp     # STARK proof verification
├── stark_proof.hpp            # AIR constraints + proof types
├── goldilocks.hpp/cpp         # Prime field arithmetic
├── fri.hpp/cpp                # FRI polynomial commitment
├── polynomial.hpp/cpp         # Polynomial operations + NTT
├── neural_layer.hpp/cpp       # Quantized neural operations
├── quantizer.hpp/cpp          # Float → Int8 quantization
└── reasoning_engine.hpp/cpp   # LLM output parsing
```

## References

- [STARK Paper](https://eprint.iacr.org/2018/046) — Scalable Transparent ARguments of Knowledge
- [Goldilocks Field](https://polygon.technology/blog/plonky2) — Polygon Plonky2
- [FRI Protocol](https://eccc.weizmann.ac.il/report/2017/134/) — Fast Reed-Solomon IOP
- [DeepSeek R1](https://arxiv.org/abs/2401.02954) — Reasoning LLM architecture

## License

Business Source License 1.1 — See [LICENSE](LICENSE) for details.

---

**Part of [GLOFICA](https://glofica.com) — Global Financial Operating System**
