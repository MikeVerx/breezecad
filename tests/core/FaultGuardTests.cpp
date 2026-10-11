#include "core/FaultGuard.h"

#include <doctest.h>

#include <stdexcept>
#include <thread>

namespace {

volatile int* volatile gNowhere = nullptr;

void Fault() { *gNowhere = 1; }

} // namespace

TEST_CASE("a fault inside guarded work ends that work, not the program") {
    std::string what;
    int reached = 0;
    CHECK_FALSE(bz::guard::RunGuarded(
        [&] {
            reached = 1;
            Fault();
            reached = 2;
        },
        &what));
    CHECK(reached == 1);
    CHECK_FALSE(what.empty());

    // The guard is ready again, and work without faults just runs.
    CHECK_FALSE(bz::guard::RunGuarded(Fault));
    int sum = 0;
    CHECK(bz::guard::RunGuarded([&] { sum = 2 + 2; }));
    CHECK(sum == 4);
}

TEST_CASE("exceptions pass through guarded work, and guards nest") {
    CHECK_THROWS_AS(bz::guard::RunGuarded([] { throw std::runtime_error("x"); }), std::runtime_error);
    bool inner = true;
    CHECK(bz::guard::RunGuarded([&] { inner = bz::guard::RunGuarded(Fault); }));
    CHECK_FALSE(inner);
    CHECK_FALSE(bz::guard::RunGuarded(Fault));   // the outer guard is still in place after the inner one
}

TEST_CASE("each thread has its own guard") {
    bool other = true;
    std::thread t([&] { other = bz::guard::RunGuarded(Fault); });
    t.join();
    CHECK_FALSE(other);
}
