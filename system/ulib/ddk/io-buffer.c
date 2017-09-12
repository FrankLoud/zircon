// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/io-buffer.h>
#include <ddk/driver.h>
#include <zircon/assert.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>
#include <limits.h>
#include <stdio.h>

#include "internal.h"

static zx_status_t io_buffer_init_common(io_buffer_t* buffer, zx_handle_t vmo_handle, size_t size,
                                         zx_off_t offset, uint32_t flags) {
    zx_vaddr_t virt;

    zx_status_t status = zx_vmar_map(zx_vmar_root_self(), 0, vmo_handle, 0, size, flags, &virt);
    if (status != ZX_OK) {
        printf("io_buffer: zx_vmar_map failed %d size: %zu\n", status, size);
        zx_handle_close(vmo_handle);
        return status;
    }

    zx_paddr_t phys;
    if (io_default_bti == ZX_HANDLE_INVALID) {
        size_t lookup_size = size < PAGE_SIZE ? size : PAGE_SIZE;
        status = zx_vmo_op_range(vmo_handle, ZX_VMO_OP_LOOKUP, 0, lookup_size, &phys, sizeof(phys));
    } else {
        uint32_t actual_extents;
        status = zx_bti_pin(io_default_bti, vmo_handle, 0, ROUNDUP(size, PAGE_SIZE),
                            ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE,
                            &phys, 1, &actual_extents);
        // We should remove this assert by tracking the length of this array
        // explicitly in buffer.
        ZX_DEBUG_ASSERT(status != ZX_OK || actual_extents == 1);
    }
    if (status != ZX_OK) {
        printf("io_buffer: zx_vmo_op_range failed %d size: %zu\n", status, size);
        zx_vmar_unmap(zx_vmar_root_self(), virt, size);
        zx_handle_close(vmo_handle);
        return status;
    }

    buffer->vmo_handle = vmo_handle;
    buffer->size = size;
    buffer->offset = offset;
    buffer->virt = (void *)virt;
    buffer->phys = phys;
    return ZX_OK;
}

zx_status_t io_buffer_init_aligned(io_buffer_t* buffer, size_t size, uint32_t alignment_log2, uint32_t flags) {
    if (size == 0) {
        return ZX_ERR_INVALID_ARGS;
    }
    if (flags != IO_BUFFER_RO && flags != IO_BUFFER_RW) {
        return ZX_ERR_INVALID_ARGS;
    }

    zx_handle_t vmo_handle;
    zx_status_t status = zx_vmo_create_contiguous(get_root_resource(), size, alignment_log2, &vmo_handle);
    if (status != ZX_OK) {
        printf("io_buffer: zx_vmo_create_contiguous failed %d\n", status);
        return status;
    }

    return io_buffer_init_common(buffer, vmo_handle, size, 0, flags);
}

zx_status_t io_buffer_init(io_buffer_t* buffer, size_t size, uint32_t flags) {
    // A zero alignment gets interpreted as PAGE_SIZE_SHIFT.
    return io_buffer_init_aligned(buffer, size, 0, flags);
}

zx_status_t io_buffer_init_vmo(io_buffer_t* buffer, zx_handle_t vmo_handle, zx_off_t offset,
                               uint32_t flags) {
    uint64_t size;

    if (flags != IO_BUFFER_RO && flags != IO_BUFFER_RW) {
        return ZX_ERR_INVALID_ARGS;
    }

    zx_status_t status = zx_handle_duplicate(vmo_handle, ZX_RIGHT_SAME_RIGHTS, &vmo_handle);
    if (status != ZX_OK) return status;

    status = zx_vmo_get_size(vmo_handle, &size);
    if (status != ZX_OK) {
        zx_handle_close(vmo_handle);
        return status;
    }

    return io_buffer_init_common(buffer, vmo_handle, size, offset, flags);
}

zx_status_t io_buffer_init_physical(io_buffer_t* buffer, zx_paddr_t addr, size_t size,
                                    zx_handle_t resource, uint32_t cache_policy) {
    zx_handle_t vmo_handle;
    zx_status_t status = zx_vmo_create_physical(resource, addr, size, &vmo_handle);
    if (status != ZX_OK) {
        printf("io_buffer: zx_vmo_create_physical failed %d\n", status);
        return status;
    }

    status = zx_vmo_set_cache_policy(vmo_handle, cache_policy);
    if (status != ZX_OK) {
        printf("io_buffer: zx_vmo_set_cache_policy failed %d\n", status);
        zx_handle_close(vmo_handle);
        return status;
    }

    uint32_t flags = ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE | ZX_VM_FLAG_MAP_RANGE;
    zx_vaddr_t virt;
    status = zx_vmar_map(zx_vmar_root_self(), 0, vmo_handle, 0, size, flags, &virt);
    if (status != ZX_OK) {
        printf("io_buffer: zx_vmar_map failed %d size: %zu\n", status, size);
        zx_handle_close(vmo_handle);
        return status;
    }

    buffer->vmo_handle = vmo_handle;
    buffer->size = size;
    buffer->offset = 0;
    buffer->virt = (void *)virt;
    buffer->phys = addr;
    return ZX_OK;
}

void io_buffer_release(io_buffer_t* buffer) {
    if (buffer->vmo_handle) {
        if (io_default_bti != ZX_HANDLE_INVALID) {
            zx_status_t status = zx_bti_unpin(io_default_bti, &buffer->phys, 1);
            ZX_DEBUG_ASSERT(status == ZX_OK);
        }

        zx_vmar_unmap(zx_vmar_root_self(), (uintptr_t)buffer->virt, buffer->size);
        zx_handle_close(buffer->vmo_handle);
        buffer->vmo_handle = ZX_HANDLE_INVALID;
    }
}

zx_status_t io_buffer_cache_op(io_buffer_t* buffer, const uint32_t op,
                               const zx_off_t offset, const size_t size) {
    return zx_vmo_op_range(buffer->vmo_handle, op, offset, size, NULL, 0);
}
