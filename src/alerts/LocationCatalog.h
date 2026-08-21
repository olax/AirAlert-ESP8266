#pragma once
// ILocationCatalog over the generated PROGMEM table (SPEC 13, 16).
#include "airalert/Location.h"
#include "LocationTable.h"

class LocationCatalog : public airalert::ILocationCatalog {
public:
    bool findByUid(uint16_t uid, airalert::Location& out) const override {
        // table is sorted by uid -> binary search
        size_t lo = 0, hi = kLocationTableSize;
        while (lo < hi) {
            const size_t mid = (lo + hi) / 2;
            if (kLocationTable[mid].uid < uid) lo = mid + 1;
            else hi = mid;
        }
        if (lo >= kLocationTableSize || kLocationTable[lo].uid != uid) return false;
        const LocationRow& r = kLocationTable[lo];
        out.uid = r.uid;
        out.type = static_cast<airalert::LocationType>(r.type);
        out.oblastUid = r.oblastUid;
        out.raionUid = r.raionUid;
        return true;
    }
};
