#pragma once

namespace airalert {

// A 304 is usable only when the caller still owns a validated snapshot for
// the current token and location selection.
class ApiSnapshotCache {
public:
    void invalidate() { valid_ = false; }
    void commitValidSnapshot() { valid_ = true; }
    bool canAcceptNotModified() const { return valid_; }

private:
    bool valid_ = false;
};

} // namespace airalert
