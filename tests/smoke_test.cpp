#include "test_framework.hpp"

SAFETY_CRIT_TEST_CASE(AlwaysPasses) {
    SAFETY_CRIT_ASSERT(true);
}