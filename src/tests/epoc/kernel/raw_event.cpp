#include <catch2/catch.hpp>
#include <kernel/raw_event.h>

using eka2l1::kernel::raw_event_queue;
using eka2l1::epoc::raw_event_eka1;

TEST_CASE("raw event ownership and outstanding request survive invalid callers", "[raw-event]") {
    raw_event_queue q;
    REQUIRE(q.request(1, 100, 200) == raw_event_queue::not_captured);
    REQUIRE(q.capture(1) == 0);
    REQUIRE(q.capture(1) == raw_event_queue::already_captured);
    REQUIRE(q.capture(2) == raw_event_queue::already_captured);
    REQUIRE(q.request(2, 300, 400) == raw_event_queue::not_captured);
    REQUIRE(q.request(1, 100, 200) == 0);
    REQUIRE(q.request(1, 300, 400) == raw_event_queue::request_pending);
    REQUIRE(q.status() == 100);
    REQUIRE(q.buffer() == 200);
    q.cancel();
    REQUIRE(q.owner() == 1);
    REQUIRE(q.status() == 0);
    REQUIRE(q.request(1, 300, 400) == 0);
    q.reset();
    REQUIRE(q.owner() == 0);
    REQUIRE(q.status() == 0);
    REQUIRE(q.capture(2) == 0);
}

TEST_CASE("raw event burst preserves every key edge and modifier boundary", "[raw-event]") {
    raw_event_queue q;
    q.capture(1);
    for (int i = 0; i < 600; ++i) {
        raw_event_eka1 event{};
        event.type_ = (i & 1) ? 4 : 3;
        event.time_in_ticks_ = i;
        event.data_.scancode_ = i / 2;
        REQUIRE(q.add(event) == 0);
    }
    REQUIRE_FALSE(q.take());
    for (int i = 0; i < 600; ++i) {
        REQUIRE(q.request(1, 100, 200) == 0);
        auto event = q.take();
        REQUIRE(event);
        REQUIRE(event->time_in_ticks_ == i);
        REQUIRE(event->data_.scancode_ == i / 2);
        REQUIRE(event->type_ == ((i & 1) ? 4 : 3));
        q.cancel();
    }
    REQUIRE(q.size() == 0);
}

TEST_CASE("raw pointer motion coalesces without crossing a button or key edge", "[raw-event]") {
    raw_event_queue q;
    q.capture(1);
    raw_event_eka1 event{};
    event.type_ = 1;
    q.add(event);
    event.data_.pos_.x_ = 70;
    q.add(event);
    event.type_ = 10;
    q.add(event);
    event.type_ = 1;
    q.add(event);
    REQUIRE(q.size() == 3);
    q.request(1, 100, 200);
    REQUIRE(q.take()->data_.pos_.x_ == 70);
    q.cancel();
    REQUIRE(q.size() == 2);
    q.reset();
    REQUIRE(q.size() == 0);
    event.type_ = 3;
    for (std::size_t i = 0; i < raw_event_queue::capacity; ++i) REQUIRE(q.add(event) == 0);
    REQUIRE(q.add(event) == eka2l1::epoc::error_overflow);
    REQUIRE(q.size() == raw_event_queue::capacity);
}
