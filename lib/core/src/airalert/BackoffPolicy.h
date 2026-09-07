#pragma once
#include <cstdint>

namespace airalert {

// Poll pacing per SPEC 10, 163, 165. Pure: randomness is injected.
class BackoffPolicy {
public:
    enum class Outcome : uint8_t {
        Success,      // 200
        NotModified,  // 304 — successful contact (SPEC 8)
        NetError,     // timeout / DNS / TLS / Wi-Fi
        HttpError,    // 5xx and other unexpected codes
        ParseError,   // invalid JSON (SPEC 9)
        RateLimited,  // 429 (SPEC 165)
        AuthError     // 401 streak = bad key (SPEC 163); a lone 401 is rate limiting -> Success
    };

    struct Config {
        uint32_t pollMs = 15000;
        uint32_t maxMs = 120000;
        uint32_t rateLimitMinMs = 60000;
        uint32_t authErrorMs = 300000; // 5 min (SPEC 163)
        uint8_t jitterPct = 10;
    };

    BackoffPolicy();
    explicit BackoffPolicy(const Config& cfg);
    void setConfig(const Config& cfg) { cfg_ = cfg; }

    // rnd: any byte of entropy for jitter (pure/testable).
    uint32_t next(Outcome o, uint32_t retryAfterSec, uint8_t rnd) {
        uint32_t base;
        switch (o) {
            case Outcome::Success:
            case Outcome::NotModified:
                errors_ = 0;
                base = cfg_.pollMs;
                break;
            case Outcome::AuthError:
                errors_ = 0;
                base = cfg_.authErrorMs;
                break;
            case Outcome::RateLimited: {
                errors_ = 0;
                const uint32_t ra = retryAfterSec * 1000u;
                base = ra > cfg_.rateLimitMinMs ? ra : cfg_.rateLimitMinMs;
                break;
            }
            default: { // 15 -> 30 -> 60 -> 120 -> 120 ... (SPEC 10)
                const uint8_t shift = errors_ < 3 ? errors_ : 3;
                base = cfg_.pollMs << shift;
                if (base > cfg_.maxMs) base = cfg_.maxMs;
                if (errors_ < 10) ++errors_;
                break;
            }
        }
        // jitter: base ± jitterPct%
        const uint32_t j = base / 100u * cfg_.jitterPct;
        return base - j + static_cast<uint32_t>(static_cast<uint64_t>(2 * j) * rnd / 255u);
    }

    uint8_t consecutiveErrors() const { return errors_; }

private:
    Config cfg_;
    uint8_t errors_ = 0;
};

inline BackoffPolicy::BackoffPolicy() = default;
inline BackoffPolicy::BackoffPolicy(const Config& cfg) : cfg_(cfg) {}

} // namespace airalert
