/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * SPDX-License-Identifier: 0BSD
 */

#include "dynarmic/common/memory_pool.h"

#include <cstdlib>

namespace Dynarmic::Common {

Pool::Pool(size_t object_size, size_t initial_pool_size)
        : object_size(object_size), slab_size(initial_pool_size) {
    AllocateNewSlab();
}

namespace {

// Every IR::Block owns a Pool whose first slab holds 4096 instructions, a few
// hundred KiB, and blocks are created and destroyed once per compiled guest
// block. Allocators that serve such sizes with fresh mappings (e.g. hardened
// allocators) then pay a map, unmap and page-protection change per compile.
// Keep a few freed slabs per thread for reuse instead.
struct SlabCache {
    static constexpr size_t max_slabs = 4;
    size_t bytes = 0;
    std::vector<char*> slabs;

    ~SlabCache() {
        for (char* slab : slabs) {
            std::free(slab);
        }
    }

    char* Take(size_t size) {
        if (size != bytes || slabs.empty()) {
            return static_cast<char*>(std::malloc(size));
        }
        char* slab = slabs.back();
        slabs.pop_back();
        return slab;
    }

    void Give(char* slab, size_t size) {
        if (size != bytes) {
            for (char* old : slabs) {
                std::free(old);
            }
            slabs.clear();
            bytes = size;
        }
        if (slabs.size() < max_slabs) {
            slabs.push_back(slab);
        } else {
            std::free(slab);
        }
    }
};

thread_local SlabCache slab_cache;

}  // namespace

Pool::~Pool() {
    slab_cache.Give(current_slab, object_size * slab_size);

    for (char* slab : slabs) {
        slab_cache.Give(slab, object_size * slab_size);
    }
}

void* Pool::Alloc() {
    if (remaining == 0) {
        slabs.push_back(current_slab);
        AllocateNewSlab();
    }

    void* ret = static_cast<void*>(current_ptr);
    current_ptr += object_size;
    remaining--;

    return ret;
}

void Pool::AllocateNewSlab() {
    current_slab = slab_cache.Take(object_size * slab_size);
    current_ptr = current_slab;
    remaining = slab_size;
}

}  // namespace Dynarmic::Common
