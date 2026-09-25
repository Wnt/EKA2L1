#include <kernel/kernel.h>
#include <kernel/thread.h>
#include <utils/des.h>
#include <utils/reqsts.h>
#include <cstdlib>
#include <common/log.h>

namespace eka2l1 {
    bool kernel_system::rom_raw_input_enabled() const {
        return get_epoc_version() == epocver::epoc7 && std::getenv("EKA2L1_ROM_WSERV");
    }

    void kernel_system::capture_raw_event_hook() {
        const int panic = raw_events_.capture(reinterpret_cast<std::uintptr_t>(crr_thread()));
        if (panic) crr_thread()->kill(kernel::entity_exit_type::panic, u"KERN-EXEC", panic);
    }

    void kernel_system::detach_raw_event_owner(kernel::thread *thread) {
        if (raw_events_.owner() == reinterpret_cast<std::uintptr_t>(thread)) raw_events_.reset();
    }

    void kernel_system::cancel_raw_event() {
        if (raw_events_.owner() != reinterpret_cast<std::uintptr_t>(crr_thread())) {
            crr_thread()->kill(kernel::entity_exit_type::panic, u"KERN-EXEC", kernel::raw_event_queue::not_captured);
            return;
        }
        if (raw_events_.status()) {
            auto status = eka2l1::ptr<epoc::request_status>(raw_events_.status());
            raw_events_.cancel(); // clear before waking the owner
            epoc::notify_info(status, crr_thread()).complete(epoc::error_cancel);
        }
    }

    void kernel_system::release_raw_event_hook() {
        auto *owner = crr_thread();
        cancel_raw_event();
        detach_raw_event_owner(owner);
    }

    void kernel_system::request_raw_event(address buffer, address status) {
        const int panic = raw_events_.request(reinterpret_cast<std::uintptr_t>(crr_thread()), status, buffer);
        if (panic) {
            crr_thread()->kill(kernel::entity_exit_type::panic, u"KERN-EXEC", panic);
            return;
        }
        auto *request = eka2l1::ptr<epoc::request_status>(status).get(crr_process());
        if (!request) {
            raw_events_.cancel();
            crr_thread()->kill(kernel::entity_exit_type::panic, u"KERN-EXEC", 3);
            return;
        }
        request->set(epoc::request_status::pending_status, true);
        deliver_raw_event();
    }

    void kernel_system::deliver_raw_event() {
        auto event = raw_events_.take();
        if (!event) return;
        auto *owner = reinterpret_cast<kernel::thread *>(raw_events_.owner());
        auto status = eka2l1::ptr<epoc::request_status>(raw_events_.status());
        auto *buffer = eka2l1::ptr<epoc::des8>(raw_events_.buffer()).get(owner->owning_process());
        int result = epoc::error_bad_descriptor;
        if (buffer && buffer->get_descriptor_type() == epoc::buf
            && buffer->get_max_length(owner->owning_process()) >= sizeof(*event)
            && buffer->get_pointer(owner->owning_process())) {
            buffer->assign(owner->owning_process(), reinterpret_cast<const std::uint8_t *>(&*event), sizeof(*event));
            result = epoc::error_none;
        }
        if (std::getenv("EKA2L1_KEYLOG")) {
            LOG_INFO(KERNEL, "RAWINPUT owner={} type={} ticks={} data=0x{:X}/0x{:X} result={}",
                owner->name(), event->type_, event->time_in_ticks_, event->data_.pos_.x_, event->data_.pos_.y_, result);
        }
        raw_events_.cancel();
        epoc::notify_info(status, owner).complete(result);
    }

    int kernel_system::add_raw_event(const epoc::raw_event_eka1 &event) {
        const int result = raw_events_.add(event);
        reset_inactivity_time();
        deliver_raw_event();
        return result;
    }
}
