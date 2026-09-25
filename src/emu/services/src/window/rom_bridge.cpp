#include <kernel/process.h>
#include <kernel/thread.h>
#include <services/window/rom_bridge.h>
#include <utils/err.h>

namespace eka2l1 {
    static_assert(sizeof(eka2l1_rom_bridge::group) == 524, "SysState bridge wire layout");
    class rom_window_bridge_session : public service::typical_session {
    public:
        using service::typical_session::typical_session;
        ~rom_window_bridge_session() override {
            auto *bridge = server<rom_window_bridge>();
            bridge->snapshot = {};
            bridge->switches.clear();
        }
        void fetch(service::ipc_context *ctx) override {
            auto *bridge = server<rom_window_bridge>();
            if (ctx->msg->function != 0) {
                ctx->complete(epoc::error_not_supported);
                return;
            }
            if (!bridge->snapshot.assign(ctx->get_descriptor_argument_ptr(0), ctx->get_argument_data_size(0))) {
                ctx->complete(epoc::error_argument);
                return;
            }
            int target = 0;
            if (!bridge->switches.empty()) {
                target = bridge->switches.front();
                bridge->switches.pop_front();
            }
            ctx->complete(target);
        }
    };
    rom_window_bridge::rom_window_bridge(eka2l1::system *sys)
        : service::typical_server(sys, "EKA2L1RomWindowBridge") {}
    void rom_window_bridge::connect(service::ipc_context &ctx) {
        if (ctx.msg->own_thr->owning_process()->get_uid() != 0x0A5C7E58) {
            ctx.complete(epoc::error_permission_denied);
            return;
        }
        if (!sessions.empty()) {
            ctx.complete(epoc::error_in_use);
            return;
        }
        create_session<rom_window_bridge_session>(&ctx);
        ctx.complete(epoc::error_none);
    }
    rom_window_bridge *get_rom_window_bridge(kernel_system *kern) {
        return static_cast<rom_window_bridge *>(kern->get_by_name<service::server>("EKA2L1RomWindowBridge"));
    }
}
