
/// @file polynomial.cpp
/// @brief Polynomial Arithmetic + NTT Implementation over Goldilocks
///
/// NTT uses Cooley-Tukey radix-2 butterfly with bit-reversal permutation.
/// Performance: O(N log N) for NTT, O(N²) for Lagrange interpolation.
/// Memory: ~16 bytes per coefficient (Goldilocks element).

#include "polynomial.hpp"
#include <algorithm>
#include <stdexcept>
#include <cmath>

namespace glofica {
namespace cortex {

// =============================================================================
// Polynomial Operations
// =============================================================================

int64_t Polynomial::degree() const {
    for (int64_t i = static_cast<int64_t>(coeffs.size()) - 1; i >= 0; --i) {
        if (!coeffs[static_cast<size_t>(i)].is_zero()) return i;
    }
    return -1; // zero polynomial
}

Goldilocks Polynomial::evaluate(Goldilocks x) const {
    if (coeffs.empty()) return Goldilocks::zero();
    
    // Horner's method: a_n*x + a_{n-1}, then *x + a_{n-2}, ...
    Goldilocks result = coeffs.back();
    for (int64_t i = static_cast<int64_t>(coeffs.size()) - 2; i >= 0; --i) {
        result = result * x + coeffs[static_cast<size_t>(i)];
    }
    return result;
}

std::vector<Goldilocks> Polynomial::evaluate_batch(const std::vector<Goldilocks>& points) const {
    std::vector<Goldilocks> results;
    results.reserve(points.size());
    for (const auto& p : points) {
        results.push_back(evaluate(p));
    }
    return results;
}

void Polynomial::resize(size_t n) {
    coeffs.resize(n, Goldilocks::zero());
}

Polynomial Polynomial::operator+(const Polynomial& other) const {
    size_t max_size = std::max(coeffs.size(), other.coeffs.size());
    std::vector<Goldilocks> result(max_size, Goldilocks::zero());
    
    for (size_t i = 0; i < coeffs.size(); ++i) {
        result[i] = result[i] + coeffs[i];
    }
    for (size_t i = 0; i < other.coeffs.size(); ++i) {
        result[i] = result[i] + other.coeffs[i];
    }
    return Polynomial(std::move(result));
}

Polynomial Polynomial::operator-(const Polynomial& other) const {
    size_t max_size = std::max(coeffs.size(), other.coeffs.size());
    std::vector<Goldilocks> result(max_size, Goldilocks::zero());
    
    for (size_t i = 0; i < coeffs.size(); ++i) {
        result[i] = result[i] + coeffs[i];
    }
    for (size_t i = 0; i < other.coeffs.size(); ++i) {
        result[i] = result[i] - other.coeffs[i];
    }
    return Polynomial(std::move(result));
}

Polynomial Polynomial::operator*(const Polynomial& other) const {
    if (coeffs.empty() || other.coeffs.empty()) return Polynomial();
    
    size_t n = coeffs.size() + other.coeffs.size() - 1;
    std::vector<Goldilocks> result(n, Goldilocks::zero());
    
    // O(n*m) naive convolution — good for small polynomials
    // For large polys, we'd use NTT-based multiplication
    for (size_t i = 0; i < coeffs.size(); ++i) {
        for (size_t j = 0; j < other.coeffs.size(); ++j) {
            result[i + j] += coeffs[i] * other.coeffs[j];
        }
    }
    return Polynomial(std::move(result));
}

Polynomial Polynomial::operator*(Goldilocks scalar) const {
    std::vector<Goldilocks> result(coeffs.size());
    for (size_t i = 0; i < coeffs.size(); ++i) {
        result[i] = coeffs[i] * scalar;
    }
    return Polynomial(std::move(result));
}

Polynomial Polynomial::divide_by_zerofier(size_t n) const {
    // Divide f(x) by Z(x) = x^n - 1
    // Using polynomial long division.
    // Optimization: Z(x) = x^n - 1 has a simple structure.
    //
    // If f(x) = q(x) * (x^n - 1), then:
    //   For each coefficient index i (from highest to lowest):
    //   q[i] = f[i+n] (shift down by n and accumulate)
    
    if (coeffs.size() <= n) {
        // Polynomial degree < n, should be zero polynomial if divisible
        return Polynomial();
    }
    
    // Work with a copy
    std::vector<Goldilocks> remainder(coeffs.begin(), coeffs.end());
    size_t q_size = coeffs.size() - n;
    std::vector<Goldilocks> quotient(q_size, Goldilocks::zero());
    
    // Long division by (x^n - 1)
    for (int64_t i = static_cast<int64_t>(remainder.size()) - 1; i >= static_cast<int64_t>(n); --i) {
        Goldilocks coeff = remainder[static_cast<size_t>(i)];
        if (!coeff.is_zero()) {
            size_t q_idx = static_cast<size_t>(i) - n;
            quotient[q_idx] = coeff;
            // Subtract coeff * (x^n - 1) shifted by q_idx
            // = coeff * x^(q_idx + n) - coeff * x^q_idx
            remainder[static_cast<size_t>(i)] = Goldilocks::zero();
            remainder[q_idx] = remainder[q_idx] + coeff; // -= (-coeff) => += coeff
        }
    }
    
    return Polynomial(std::move(quotient));
}

Polynomial Polynomial::interpolate(
    const std::vector<Goldilocks>& points,
    const std::vector<Goldilocks>& values
) {
    size_t n = points.size();
    if (n == 0) return Polynomial();
    if (n != values.size()) throw std::runtime_error("Points and values size mismatch");
    
    // Lagrange interpolation: f(x) = Σ y_i * L_i(x)
    // where L_i(x) = Π_{j≠i} (x - x_j) / (x_i - x_j)
    
    std::vector<Goldilocks> result(n, Goldilocks::zero());
    
    for (size_t i = 0; i < n; ++i) {
        // Compute the Lagrange basis polynomial L_i
        // Start with coefficient representation of L_i
        std::vector<Goldilocks> basis = {Goldilocks::one()};
        Goldilocks denom = Goldilocks::one();
        
        for (size_t j = 0; j < n; ++j) {
            if (j == i) continue;
            
            // Multiply basis by (x - x_j)
            std::vector<Goldilocks> new_basis(basis.size() + 1, Goldilocks::zero());
            for (size_t k = 0; k < basis.size(); ++k) {
                new_basis[k + 1] = new_basis[k + 1] + basis[k];           // * x
                new_basis[k] = new_basis[k] - basis[k] * points[j];       // * (-x_j)
            }
            basis = std::move(new_basis);
            
            // Accumulate denominator: (x_i - x_j)
            denom = denom * (points[i] - points[j]);
        }
        
        // Scale by y_i / denom
        Goldilocks scale = values[i] * denom.inv();
        for (size_t k = 0; k < basis.size() && k < n; ++k) {
            result[k] = result[k] + basis[k] * scale;
        }
    }
    
    return Polynomial(std::move(result));
}

bool Polynomial::is_zero() const {
    for (const auto& c : coeffs) {
        if (!c.is_zero()) return false;
    }
    return true;
}

// =============================================================================
// Evaluation Domain
// =============================================================================

EvaluationDomain::EvaluationDomain(size_t size) : size_(size) {
    // Verify power of 2
    if (size == 0 || (size & (size - 1)) != 0) {
        throw std::runtime_error("EvaluationDomain size must be power of 2");
    }
    if (size > (1ULL << 32)) {
        throw std::runtime_error("EvaluationDomain size exceeds max 2^32");
    }
    
    omega_ = Goldilocks::primitive_root_of_unity(size);
    omega_inv_ = omega_.inv();
}

Goldilocks EvaluationDomain::element(size_t i) const {
    return omega_.pow(i % size_);
}

std::vector<Goldilocks> EvaluationDomain::elements() const {
    std::vector<Goldilocks> elems;
    elems.reserve(size_);
    
    Goldilocks current = Goldilocks::one();
    for (size_t i = 0; i < size_; ++i) {
        elems.push_back(current);
        current = current * omega_;
    }
    return elems;
}

std::vector<Goldilocks> EvaluationDomain::coset(Goldilocks shift) const {
    std::vector<Goldilocks> result;
    result.reserve(size_);
    
    Goldilocks current = shift;
    for (size_t i = 0; i < size_; ++i) {
        result.push_back(current);
        current = current * omega_;
    }
    return result;
}

// =============================================================================
// NTT (Number Theoretic Transform — Cooley-Tukey Radix-2)
// =============================================================================

void EvaluationDomain::bit_reverse(std::vector<Goldilocks>& data, size_t n) {
    size_t log_n = 0;
    size_t temp = n;
    while (temp > 1) { temp >>= 1; log_n++; }
    
    for (size_t i = 0; i < n; ++i) {
        size_t rev = 0;
        size_t x = i;
        for (size_t j = 0; j < log_n; ++j) {
            rev = (rev << 1) | (x & 1);
            x >>= 1;
        }
        if (i < rev) {
            std::swap(data[i], data[rev]);
        }
    }
}

void EvaluationDomain::ntt_impl(std::vector<Goldilocks>& data, Goldilocks omega, size_t n) {
    bit_reverse(data, n);
    
    // Cooley-Tukey butterfly
    for (size_t len = 2; len <= n; len *= 2) {
        size_t half = len / 2;
        // ω for this stage: ω^(n/len)
        Goldilocks w_step = omega.pow(n / len);
        
        for (size_t start = 0; start < n; start += len) {
            Goldilocks w = Goldilocks::one();
            for (size_t j = 0; j < half; ++j) {
                Goldilocks u = data[start + j];
                Goldilocks v = data[start + j + half] * w;
                data[start + j] = u + v;
                data[start + j + half] = u - v;
                w = w * w_step;
            }
        }
    }
}

std::vector<Goldilocks> EvaluationDomain::ntt(const std::vector<Goldilocks>& coeffs) const {
    std::vector<Goldilocks> data(size_, Goldilocks::zero());
    
    // Copy coefficients (pad with zeros if needed)
    size_t copy_len = std::min(coeffs.size(), size_);
    for (size_t i = 0; i < copy_len; ++i) {
        data[i] = coeffs[i];
    }
    
    ntt_impl(data, omega_, size_);
    return data;
}

std::vector<Goldilocks> EvaluationDomain::intt(const std::vector<Goldilocks>& evals) const {
    if (evals.size() != size_) {
        throw std::runtime_error("INTT: input size must match domain size");
    }
    
    std::vector<Goldilocks> data(evals.begin(), evals.end());
    
    // INTT = NTT with inverse root, then divide by N
    ntt_impl(data, omega_inv_, size_);
    
    // Divide by N
    Goldilocks n_inv = Goldilocks(size_).inv();
    for (auto& d : data) {
        d = d * n_inv;
    }
    
    return data;
}

std::vector<Goldilocks> EvaluationDomain::evaluate(const Polynomial& poly) const {
    return ntt(poly.coeffs);
}

Polynomial EvaluationDomain::interpolate_ntt(const std::vector<Goldilocks>& evals) const {
    return Polynomial(intt(evals));
}

Polynomial EvaluationDomain::zerofier() const {
    // Z(x) = x^N - 1
    // Coefficients: [-1, 0, 0, ..., 0, 1]
    std::vector<Goldilocks> z(size_ + 1, Goldilocks::zero());
    z[0] = Goldilocks(Goldilocks::MODULUS - 1); // -1 mod p
    z[size_] = Goldilocks::one();
    return Polynomial(std::move(z));
}

} // namespace cortex
} // namespace glofica
