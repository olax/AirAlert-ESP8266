#include "LocationMatcher.h"

namespace airalert {

Coverage LocationMatcher::match(const Location& selected, const Alert& alert) const {
    if (selected.uid == 0 || alert.locationUid == 0) return Coverage::None;

    // Exact hit.
    if (alert.locationUid == selected.uid) return Coverage::Full;

    // Alert on a parent of the selected location => selected fully covered.
    if (alert.locationUid == selected.oblastUid) return Coverage::Full;
    if (selected.raionUid != 0 && alert.locationUid == selected.raionUid) return Coverage::Full;

    // Enrich the alert with catalogue hierarchy: the API only carries oblast uid,
    // the raion uid comes from our local catalogue (SPEC 13).
    uint16_t alertOblast = alert.oblastUid;
    uint16_t alertRaion = 0;
    Location known;
    if (catalog_ && catalog_->findByUid(alert.locationUid, known)) {
        if (known.oblastUid != 0) alertOblast = known.oblastUid;
        alertRaion = known.raionUid;
    }

    // Alert on a child inside the selected location => partial coverage.
    if (alertOblast == selected.uid) return Coverage::Partial;
    if (alertRaion != 0 && alertRaion == selected.uid) return Coverage::Partial;

    return Coverage::None;
}

} // namespace airalert
