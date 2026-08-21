#pragma once
#include "Alert.h"
#include "Location.h"

namespace airalert {

// Hierarchical matching (SPEC 16-19).
class LocationMatcher {
public:
    explicit LocationMatcher(const ILocationCatalog* catalog = nullptr)
        : catalog_(catalog) {}

    // Coverage of one selected location by one alert:
    //   alert on the location itself or on its parent  -> Full   (SPEC 17)
    //   alert on a child inside the selected location  -> Full? no -> Partial (SPEC 18)
    Coverage match(const Location& selected, const Alert& alert) const;

private:
    const ILocationCatalog* catalog_;
};

} // namespace airalert
