/*
 * sLaunch addition, not part of upstream haze: what the server is doing, for
 * the menu to show. Written only by the server's thread; read from any.
 */
#pragma once

#include <haze/common.hpp>
#include <atomic>

namespace haze {

    enum TransferOp : u32 {
        TransferOp_None,
        TransferOp_Receive,   /* computer -> console */
        TransferOp_Send,      /* console -> computer */
        TransferOp_Delete,
    };

    struct TransferStatus {
        std::atomic<u32> op { TransferOp_None };
        std::atomic<u64> done { 0 }, total { 0 };   /* current file; total 0 = unknown */
        std::atomic<u64> bytes { 0 };               /* every byte moved, for a rate */
        std::atomic<u32> received { 0 }, sent { 0 }, deleted { 0 };
        /* The name is a seqlock: odd while it is being written. */
        std::atomic<u32> name_seq { 0 };
        char name[128] {};
    };

    TransferStatus &GetTransferStatus();

    void TransferBegin(TransferOp op, const char *path, u64 total);
    void TransferProgress(u64 done, u64 delta);
    /* `counted` adds the file to the session's totals; partial reads and */
    /* writes are pieces of a file and are not counted. */
    void TransferEnd(bool counted);

    /* Copies the current name; false if it changed mid-copy (try next frame). */
    bool TransferName(char *out, size_t size);

}
