#pragma once

#include <utils/event.h>
#include <utils/err.h>
#include <cstdint>
#include <deque>
#include <optional>

namespace eka2l1::kernel {
    // EKA1 event hook. Access is serialized by the kernel lock. Owner tokens are
    // thread identities; the kernel drops them before the thread is destroyed.
    class raw_event_queue {
        std::uintptr_t owner_ = 0;
        std::uint32_t status_ = 0;
        std::uint32_t buffer_ = 0;
        std::deque<epoc::raw_event_eka1> events_;
    public:
        static constexpr std::size_t capacity = 4096;
        enum panic { already_captured = 12, not_captured = 13, request_pending = 20 };
        std::uintptr_t owner() const { return owner_; }
        std::uint32_t status() const { return status_; }
        std::uint32_t buffer() const { return buffer_; }
        std::size_t size() const { return events_.size(); }
        int capture(std::uintptr_t thread) {
            if (owner_) return already_captured;
            owner_ = thread;
            return 0;
        }
        int request(std::uintptr_t thread, std::uint32_t status, std::uint32_t buffer) {
            if (!owner_ || owner_ != thread) return not_captured;
            if (status_) return request_pending;
            status_ = status;
            buffer_ = buffer;
            return 0;
        }
        void cancel() { status_ = buffer_ = 0; }
        void reset() { owner_ = 0; cancel(); events_.clear(); }
        int add(const epoc::raw_event_eka1 &event) {
            // Only adjacent pointer motion is replaceable. Never purge key or
            // button edges, including when a client falls behind a host burst.
            if (event.type_ == 1 && !events_.empty() && events_.back().type_ == 1) {
                events_.back() = event;
                return epoc::error_none;
            }
            if (events_.size() == capacity) return epoc::error_overflow;
            events_.push_back(event);
            return epoc::error_none;
        }
        std::optional<epoc::raw_event_eka1> take() {
            if (!status_ || events_.empty()) return std::nullopt;
            auto event = events_.front();
            events_.pop_front();
            return event;
        }
    };
}
