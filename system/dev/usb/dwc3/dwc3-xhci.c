// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/protocol/platform-device.h>
#include <ddk/protocol/platform-devices.h>
#include "dwc3.h"


zx_status_t dwc3_xhci_set_interface(void* ctx, pbus_interface_t* interface) {
    return ZX_ERR_NOT_SUPPORTED;
}

static zx_status_t dwc3_xhci_get_protocol(void* ctx, uint32_t proto_id, void* out) {
    return ZX_ERR_NOT_SUPPORTED;
}

static zx_status_t dwc3_xhci_map_mmio(void* ctx, uint32_t index, uint32_t cache_policy,
                                         void** vaddr, size_t* size, zx_handle_t* out_handle) {
    dwc3_t* dwc = ctx;

    if (index != 0) {
        return ZX_ERR_INVALID_ARGS;
    }

    zx_handle_t handle;
    zx_status_t status = zx_handle_duplicate(dwc->mmio.handle, ZX_RIGHT_SAME_RIGHTS, &handle);
    if (status != ZX_OK) return status;

    
    status = zx_vmar_map(zx_vmar_root_self(), 0, handle, 0, dwc->mmio.size,
                         ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE | ZX_VM_FLAG_MAP_RANGE,
                         (uintptr_t*)vaddr);
    if (status != ZX_OK) {
        zx_handle_close(handle);
        return status;
    }

    *size = dwc->mmio.size;
    *out_handle = handle;
    return ZX_OK;
}

static zx_status_t dwc3_xhci_map_interrupt(void* ctx, uint32_t index, zx_handle_t* out_handle) {
    dwc3_t* dwc = ctx;

    if (index != 0) {
        return ZX_ERR_INVALID_ARGS;
    }
//FIXME this doesn't work    return zx_handle_duplicate(dwc->irq_handle, ZX_RIGHT_SAME_RIGHTS, out_handle);
    *out_handle = dwc->irq_handle;
    return ZX_OK;
}

static platform_device_protocol_ops_t dwc3_xhci_proto_ops = {
    .set_interface = dwc3_xhci_set_interface,
    .get_protocol = dwc3_xhci_get_protocol,
    .map_mmio = dwc3_xhci_map_mmio,
    .map_interrupt = dwc3_xhci_map_interrupt,
};

static void dwc3_xhci_release(void* ctx) {
}

static zx_protocol_device_t dwc3_xhci_proto = {
    .version = DEVICE_OPS_VERSION,
    .release = dwc3_xhci_release,
};

zx_status_t dwc3_start_xhci(dwc3_t* dwc) {
    zx_device_prop_t props[] = {
        {BIND_PLATFORM_DEV_VID, 0, PDEV_VID_GENERIC},
        {BIND_PLATFORM_DEV_PID, 0, PDEV_PID_GENERIC},
        {BIND_PLATFORM_DEV_DID, 0, PDEV_DID_USB_XHCI},
    };

    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "dwc3-xhci",
        .ctx = dwc,
        .ops = &dwc3_xhci_proto,
        .proto_id = ZX_PROTOCOL_PLATFORM_DEV,
        .proto_ops = &dwc3_xhci_proto_ops,
        .props = props,
        .prop_count = countof(props),
    };

    return device_add(dwc->parent, &args, &dwc->xhci_dev);
}

zx_status_t dwc3_stop_xhci(dwc3_t* dwc) {
    return ZX_OK;
}

