//===-----------------------------------------------------------------------===//
//
// This file is part of tam-cpp, copyright (c) Ian Knight 2025.
//
// tam-cpp is free software: you can redistribute it and/or modify it under the
// terms of the GNU General Public License as published by the Free Software
// Foundation, either version 3 of the License, or (at your option) any later
// version.
//
// tam-cpp is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
// A PARTICULAR PURPOSE. See the GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along
// with tam-cpp. If not, see <https://www.gnu.org/licenses/>.
//
//===-----------------------------------------------------------------------===//
//
/// @file heap.cc
/// This file defines the `Allocate` and `Free` methods of `TamEmulator`.
//
//===-----------------------------------------------------------------------===//

#include <assert.h>

#include <map>
#include <format>

#include "error.h"
#include "tam.h"

namespace tam {

/// Iterates through all existing free blocks in the heap to try and find one of
/// the correct size. If none is found, the heap is expanded.
TamAddr TamEmulator::Allocate(int n) {
    // try to find unallocated space inside heap
    for (auto block_iter = this->free_blocks.begin(),
              iter_end = this->free_blocks.end();
         block_iter != iter_end; ++block_iter) {
        assert(block_iter->first > this->registers[HT]);

        if (block_iter->second < n)  // block not big enough
            continue;

        TamAddr block_start = block_iter->first;

        this->allocated_blocks.emplace(block_start, n);
        if (block_iter->second > n)
            this->free_blocks.emplace(block_start + n, block_iter->second - n);
        this->free_blocks.erase(block_iter);

        return block_start;
    }

    // expand heap
    this->registers[HT] -= n;
    if (this->registers[HT] <= this->registers[ST])
        throw std::runtime_error(std::format("heap overflow at loc {} trying to allocate {}-word block", this->registers[CP] - 1, n));

    this->allocated_blocks.emplace(this->registers[HT] + 1, n);
    return this->registers[HT] + 1;
}

/// Attempts to locate an allocated block of the given address and size.
/// If the block was at the end of the heap then the heap is contracted,
/// otherwise the freed block is added to the list of available blocks.
void TamEmulator::Free(TamAddr addr, TamData size) {
    bool addr_outside_heap = addr <= this->registers[HT];
    if (addr_outside_heap)
        throw std::runtime_error(std::format("data access violation at loc {}: attempted to free heap block at {:04X} which is outside the heap", this->registers[CP] - 1, addr));

    bool addr_not_allocated = !this->allocated_blocks.count(addr);
    if (addr_not_allocated)
        throw std::runtime_error(std::format("data access violation at loc {}: attempted to free heap block at {:04X} which is not allocated", this->registers[CP] - 1, addr));

    for (auto block_iter = this->allocated_blocks.begin(), iter_end = this->allocated_blocks.end(); block_iter != iter_end; ++block_iter) {
        assert(block_iter->first > this->registers[HT]);

        if (block_iter->first != addr)  // not the specified block
            continue;

        bool wrong_size = block_iter->second != size;
        if (wrong_size)
            throw std::runtime_error(std::format("data access violation at loc {}: attempted to free heap block of size {} at {:04X} which is actually of size {}", this->registers[CP] - 1, size, addr, block_iter->second));

        if (addr == this->registers[HT] + 1) {
            // block on top of heap, shrink heap
            this->registers[HT] += block_iter->second;
        } else {
            // mark block as available
            this->free_blocks.emplace(block_iter->first, block_iter->second);
        }
        this->allocated_blocks.erase(block_iter);
        break;
    }
}

}  // namespace tam
