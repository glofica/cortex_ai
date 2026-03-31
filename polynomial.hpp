
/// @file polynomial.hpp
/// @brief Polynomial Arithmetic over Goldilocks Field (STARK Foundation)
///
/// =============================================================================
///                    CORTEX IA — POLYNOMIAL ENGINE
/// =============================================================================
///
/// Provides polynomial operations needed for FRI and STARK:
///   - Evaluation (Horner's method + NTT for batch)
///   - Interpolation (INTT)
///   - Arithmetic (+, -, *)
///   - Division by zerofier (x^N - 1)
///
/// The EvaluationDomain class manages roots of unity for NTT/INTT:
///   ω_N = 7^((p-1)/N) mod p   (Goldilocks primitive N-th root)
///
/// =============================================================================

#ifndef GLOFICA_CORTEX_POLYNOMIAL_HPP
#define GLOFICA_CORTEX_POLYNOMIAL_HPP

#include "goldilocks.hpp"
#include <vector>
#include <cstddef>

namespace glofica {
namespace cortex {

// =============================================================================
// Polynomial over F_p
// =============================================================================

/// @brief Polynomial with coefficients in the Goldilocks field
/// Represented as a_0 + a_1*x + a_2*x^2 + ... + a_n*x^n
class Polynomial {
public:
    std::vector<Goldilocks> coeffs;  ///< Coefficients [a_0, a_1, ..., a_n]
    
    Polynomial() = default;
    explicit Polynomial(std::vector<Goldilocks> c) : coeffs(std::move(c)) {}
    
    /// @brief Degree of the polynomial (-1 for zero polynomial)
    int64_t degree() const;
    
    /// @brief Number of coefficients
    size_t size() const { return coeffs.size(); }
    
    /// @brief Evaluate at a single point using Horner's method — O(n)
    Goldilocks evaluate(Goldilocks x) const;
    
    /// @brief Evaluate at multiple points (batch) — O(n*m)
    std::vector<Goldilocks> evaluate_batch(const std::vector<Goldilocks>& points) const;
    
    /// @brief Pad or trim to exactly n coefficients
    void resize(size_t n);
    
    // =========================================================================
    // Arithmetic
    // =========================================================================
    
    Polynomial operator+(const Polynomial& other) const;
    Polynomial operator-(const Polynomial& other) const;
    Polynomial operator*(const Polynomial& other) const;
    
    /// @brief Scalar multiplication: all coefficients * scalar
    Polynomial operator*(Goldilocks scalar) const;
    
    /// @brief Divide by zerofier Z(x) = x^n - 1
    /// @pre This polynomial must be divisible by the zerofier
    /// @return Quotient Q(x) such that this(x) = Q(x) * (x^n - 1)
    Polynomial divide_by_zerofier(size_t n) const;
    
    /// @brief Interpolate from point-value pairs (Lagrange) — O(n^2)
    /// For small polynomials. For large, use EvaluationDomain::interpolate_ntt
    static Polynomial interpolate(
        const std::vector<Goldilocks>& points,
        const std::vector<Goldilocks>& values
    );
    
    /// @brief Check if polynomial is zero
    bool is_zero() const;
};

// =============================================================================
// Evaluation Domain (Roots of Unity for NTT/STARK)
// =============================================================================

/// @brief Evaluation domain using multiplicative subgroup of F_p
/// Domain D = {1, ω, ω², ..., ω^(N-1)} where ω is primitive N-th root of unity
class EvaluationDomain {
public:
    /// @brief Construct domain of given size (must be power of 2)
    explicit EvaluationDomain(size_t size);
    
    /// @brief Domain size
    size_t size() const { return size_; }
    
    /// @brief Generator (ω)
    Goldilocks generator() const { return omega_; }
    
    /// @brief Get i-th element: ω^i
    Goldilocks element(size_t i) const;
    
    /// @brief Get all domain elements
    std::vector<Goldilocks> elements() const;
    
    /// @brief Coset: shift * {1, ω, ω², ...}
    /// Used by FRI for evaluation on shifted domain
    std::vector<Goldilocks> coset(Goldilocks shift) const;
    
    // =========================================================================
    // NTT / INTT (Number Theoretic Transform)
    // =========================================================================
    
    /// @brief Forward NTT: coefficients → evaluations on domain
    /// Transforms [a_0, a_1, ..., a_{n-1}] to [f(ω^0), f(ω^1), ..., f(ω^{n-1})]
    std::vector<Goldilocks> ntt(const std::vector<Goldilocks>& coeffs) const;
    
    /// @brief Inverse NTT: evaluations → coefficients
    /// Transforms evaluations back to coefficient form
    std::vector<Goldilocks> intt(const std::vector<Goldilocks>& evals) const;
    
    /// @brief Evaluate polynomial on this domain via NTT
    std::vector<Goldilocks> evaluate(const Polynomial& poly) const;
    
    /// @brief Interpolate evaluations on this domain via INTT
    Polynomial interpolate_ntt(const std::vector<Goldilocks>& evals) const;
    
    /// @brief Compute zerofier Z(x) = x^N - 1 for this domain
    Polynomial zerofier() const;

private:
    size_t size_;           ///< Domain size (power of 2)
    Goldilocks omega_;      ///< Primitive N-th root of unity
    Goldilocks omega_inv_;  ///< Inverse of omega (for INTT)
    
    /// @brief Cooley-Tukey butterfly NTT (in-place)
    static void ntt_impl(std::vector<Goldilocks>& data, Goldilocks omega, size_t n);
    
    /// @brief Bit-reversal permutation
    static void bit_reverse(std::vector<Goldilocks>& data, size_t n);
};

} // namespace cortex
} // namespace glofica

#endif // GLOFICA_CORTEX_POLYNOMIAL_HPP
