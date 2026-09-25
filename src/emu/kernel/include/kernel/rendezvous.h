// Copyright (c) 2026 EKA2L1 Team. SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <functional>
#include <utility>
#include <vector>

namespace eka2l1::kernel {
    // Pending host observers of a guest process startup. A notification consumes only
    // the requests already armed; callbacks may arm requests for a later rendezvous.
    class rendezvous_callbacks {
        std::vector<std::function<void(int)>> pending_;
    public:
        void arm(std::function<void(int)> callback) {
            pending_.push_back(std::move(callback));
        }
        void complete(int result) {
            auto callbacks = std::move(pending_);
            pending_.clear();
            for (auto &callback : callbacks) callback(result);
        }
    };
}
