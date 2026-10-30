/*
 * sLaunch addition, not part of upstream haze. See transfer_status.hpp.
 */
#include <haze.hpp>
#include <haze/transfer_status.hpp>

namespace haze {

    namespace {

        constinit TransferStatus g_status;

    }

    TransferStatus &GetTransferStatus() { return g_status; }

    void TransferBegin(TransferOp op, const char *path, u64 total) {
        g_status.name_seq.fetch_add(1, std::memory_order_acq_rel);
        std::strncpy(g_status.name, path, sizeof(g_status.name) - 1);
        g_status.name[sizeof(g_status.name) - 1] = '\0';
        g_status.name_seq.fetch_add(1, std::memory_order_acq_rel);

        g_status.done.store(0, std::memory_order_relaxed);
        g_status.total.store(total, std::memory_order_relaxed);
        g_status.op.store(op, std::memory_order_release);
    }

    void TransferProgress(u64 done, u64 delta) {
        g_status.done.store(done, std::memory_order_relaxed);
        g_status.bytes.fetch_add(delta, std::memory_order_relaxed);
    }

    void TransferEnd(bool counted) {
        const u32 op = g_status.op.exchange(TransferOp_None, std::memory_order_acq_rel);
        if (!counted) return;
        switch (op) {
            case TransferOp_Receive: g_status.received.fetch_add(1); break;
            case TransferOp_Send:    g_status.sent.fetch_add(1);     break;
            case TransferOp_Delete:  g_status.deleted.fetch_add(1);  break;
            default: break;
        }
    }

    bool TransferName(char *out, size_t size) {
        const u32 s0 = g_status.name_seq.load(std::memory_order_acquire);
        if (s0 & 1) return false;
        std::strncpy(out, g_status.name, size - 1);
        out[size - 1] = '\0';
        std::atomic_thread_fence(std::memory_order_acquire);
        return g_status.name_seq.load(std::memory_order_relaxed) == s0;
    }

}
