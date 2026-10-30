/*
 * Copyright (c) Atmosphère-NX
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <haze.hpp>

namespace haze {

    namespace {

        /* sLaunch: haze runs inside the menu here, not as its own program, so */
        /* it cannot claim everything but 30MiB. The heap only holds the object */
        /* database (one entry per file or folder the computer has listed). */
        static constexpr size_t HeapBlockSize = 4_MB;

    }

    void PtpObjectHeap::Initialize() {
        /* If we're already initialized, skip re-initialization. */
        if (m_heap_block_size != 0) {
            return;
        }

        m_heap_block_size = HeapBlockSize;

        /* Allocate the memory. */
        for (size_t i = 0; i < NumHeapBlocks; i++) {
            m_heap_blocks[i] = std::malloc(m_heap_block_size);
            /* sLaunch: out of memory fails the transfer, not the whole menu. */
            /* A zero block size makes every Allocate() return nullptr. */
            if (m_heap_blocks[i] == nullptr) {
                for (size_t j = 0; j < i; j++) {
                    std::free(m_heap_blocks[j]);
                    m_heap_blocks[j] = nullptr;
                }
                m_heap_block_size = 0;
                return;
            }
        }

        /* Set the address to allocate from. */
        m_next_address = m_heap_blocks[0];
    }

    void PtpObjectHeap::Finalize() {
        if (m_heap_block_size == 0) {
            return;
        }

        /* Tear down the heap, allowing a subsequent call to Initialize() if desired. */
        for (size_t i = 0; i < NumHeapBlocks; i++) {
            std::free(m_heap_blocks[i]);
            m_heap_blocks[i] = nullptr;
        }

        m_next_address       = nullptr;
        m_heap_block_size    = 0;
        m_current_heap_block = 0;
    }

}
