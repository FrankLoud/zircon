// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/common/hid.h>

#include <zircon/device/hidctl.h>
#include <zircon/types.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define from_hid_device(d) containerof(d, hidctl_instance_t, hiddev)

typedef struct hidctl_instance {
    zx_device_t* zxdev;
    zx_device_t* parent;
    zx_hid_device_t hiddev;

    uint8_t* hid_report_desc;
    size_t hid_report_desc_len;
} hidctl_instance_t;

typedef struct hidctl_root {
    zx_device_t* zxdev;
} hidctl_root_t;

zx_status_t hidctl_get_descriptor(zx_hid_device_t* dev, uint8_t desc_type, void** data, size_t* len) {
    if (desc_type != HID_DESC_TYPE_REPORT) {
        return ZX_ERR_NOT_SUPPORTED;
    }
    hidctl_instance_t* inst = from_hid_device(dev);
    *data = malloc(inst->hid_report_desc_len);
    memcpy(*data, inst->hid_report_desc, inst->hid_report_desc_len);
    *len = inst->hid_report_desc_len;
    return ZX_OK;
}

zx_status_t hidctl_get_report(zx_hid_device_t* dev, uint8_t rpt_type, uint8_t rpt_id, void* data, size_t len) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t hidctl_set_report(zx_hid_device_t* dev, uint8_t rpt_type, uint8_t rpt_id, void* data, size_t len) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t hidctl_get_idle(zx_hid_device_t* dev, uint8_t rpt_id, uint8_t* duration) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t hidctl_set_idle(zx_hid_device_t* dev, uint8_t rpt_id, uint8_t duration) {
    return ZX_OK;
}

zx_status_t hidctl_get_protocol(zx_hid_device_t* dev, uint8_t* protocol) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t hidctl_set_protocol(zx_hid_device_t* dev, uint8_t protocol) {
    return ZX_OK;
}

static hid_bus_ops_t hidctl_hid_ops = {
    .get_descriptor = hidctl_get_descriptor,
    .get_report = hidctl_get_report,
    .set_report = hidctl_set_report,
    .get_idle = hidctl_get_idle,
    .set_idle = hidctl_set_idle,
    .get_protocol = hidctl_get_protocol,
    .set_protocol = hidctl_set_protocol,
};

static ssize_t hidctl_set_config(hidctl_instance_t* dev, const void* in_buf, size_t in_len) {
    const hid_ioctl_config_t* cfg = in_buf;
    if (in_len < sizeof(hid_ioctl_config_t) || in_len != sizeof(hid_ioctl_config_t) + cfg->rpt_desc_len) {
        return ZX_ERR_INVALID_ARGS;
    }

    if (cfg->dev_class > HID_DEV_CLASS_LAST) {
        return ZX_ERR_INVALID_ARGS;
    }

    hid_init_device(&dev->hiddev, &hidctl_hid_ops, cfg->dev_num, cfg->boot_device, cfg->dev_class);

    dev->hid_report_desc_len = cfg->rpt_desc_len;
    dev->hid_report_desc = malloc(cfg->rpt_desc_len);
    memcpy(dev->hid_report_desc, cfg->rpt_desc, cfg->rpt_desc_len);

    zx_status_t status = hid_add_device(&_driver_hidctl, &dev->hiddev, dev->parent);
    if (status != ZX_OK) {
        hid_release_device(&dev->hiddev);
        free(dev->hid_report_desc);
        dev->hid_report_desc = NULL;
    } else {
        printf("hidctl: device added\n");
    }
    return status;
}

static ssize_t hidctl_read(void* ctx, void* buf, size_t count, zx_off_t off) {
    return 0;
}

static ssize_t hidctl_write(void* ctx, const void* buf, size_t count, zx_off_t off) {
    hidctl_instance_t* inst = ctx;
    hid_io_queue(&inst->hiddev, buf, count);
    return count;
}

static ssize_t hidctl_ioctl(void* ctx, uint32_t op,
        const void* in_buf, size_t in_len, void* out_buf, size_t out_len) {
    hidctl_instance_t* inst = ctx;
    switch (op) {
    case IOCTL_HID_CTL_CONFIG:
        return hidctl_set_config(inst, in_buf, in_len);
        break;
    }
    return ZX_ERR_NOT_SUPPORTED;
}

static void hidctl_release(void* ctx) {
    hidctl_instance_t* inst = ctx;
    hid_release_device(&inst->hiddev);
    if (inst->hid_report_desc) {
        free(inst->hid_report_desc);
        device_remove(&inst->hiddev.dev);
    }
    free(inst);
}

zx_protocol_device_t hidctl_instance_proto = {
    .read = hidctl_read,
    .write = hidctl_write,
    .ioctl = hidctl_ioctl,
    .release = hidctl_release,
};

static zx_status_t hidctl_open(void* ctx, zx_device_t** dev_out, uint32_t flags) {
    hidctl_root_t* root = ctx;
    hidctl_instance_t* inst = calloc(1, sizeof(hidctl_instance_t));
    if (inst == NULL) {
        return ZX_ERR_NO_MEMORY;
    }
    inst->parent = root->zxdev;

    zx_status_t status;
    if ((status = device_create("hidctl-inst", inst, &hidctl_instance_proto, &_driver_hidctl,
                                &inst->zxdev)) < 0) {
        free(inst);
        return status;
    }

    status = device_add_instance(inst->zxdev, root->zxdev);
    if (status != ZX_OK) {
        printf("hidctl: could not open instance: %d\n", status);
        device_destroy(inst->zxdev);
        free(inst);
        return status;
    }
    *dev_out = inst->zxdev;
    return ZX_OK;
}

static void hidctl_root_release(void* ctx) {
    hidctl_root_t* root = ctx;
    device_destroy(root->zxdev);
    free(root);
}

static zx_protocol_device_t hidctl_device_proto = {
    .open = hidctl_open,
    .release = hidctl_root_release,
};

static zx_status_t hidctl_bind(void* ctx, zx_device_t* parent, void** cookie) {
    hidctl_root_t* root = calloc(1, sizeof(hidctl_root_t));
    if (!root) {
        return ZX_ERR_NO_MEMORY;
    }

    zx_status_t status;
    if ((status = device_create("hidctl", root, &hidctl_device_proto, driver, &root->zxdev)) < 0) {
        free(root);
        return status;
    }
    if ((status = device_add(root->zxdev, parent)) < 0) {
        device_destroy(root->zxdev);
        free(root);
        return status;
    }

    return ZX_OK;
}

static zx_driver_ops_t hidctl_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = hidctl_bind,
};

ZIRCON_DRIVER_BEGIN(hidctl, hidctl_driver_ops, "zircon", "0.1", 1)
    BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_MISC_PARENT),
ZIRCON_DRIVER_END(hidctl)
