// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2019, Microsoft Corporation. All rights reserved. */

#include <stdlib.h>
#include <limits.h>
#include <util/bitmap.h>
#include <util/log.h>
#include <ndctl/libndctl.h>
#include "private.h"
#include "hyperv.h"

static struct ndctl_cmd *alloc_hyperv_cmd(struct ndctl_dimm *dimm,
		unsigned int command)
{
	struct ndctl_bus *bus = ndctl_dimm_get_bus(dimm);
	struct ndctl_ctx *ctx = ndctl_bus_get_ctx(bus);
	struct nd_pkg_hyperv *hyperv;
	struct ndctl_cmd *cmd;
	size_t size;

	if (!ndctl_dimm_is_cmd_supported(dimm, ND_CMD_CALL)) {
		dbg(ctx, "unsupported cmd\n");
		return NULL;
	}

	if (test_dimm_dsm(dimm, command) == DIMM_DSM_UNSUPPORTED) {
		dbg(ctx, "unsupported function\n");
		return NULL;
	}

	size = sizeof(*cmd) + sizeof(struct nd_pkg_hyperv);
	cmd = calloc(1, size);
	if (!cmd)
		return NULL;

	ndctl_cmd_ref(cmd);

	cmd->dimm = dimm;
	cmd->type = ND_CMD_CALL;
	cmd->size = size;
	cmd->status = 1;

	hyperv = cmd->hyperv;
	hyperv->gen.nd_family = NVDIMM_FAMILY_HYPERV;
	hyperv->gen.nd_command = command;
	hyperv->gen.nd_size_out = sizeof(hyperv->u.health_info);

	cmd->firmware_status = &hyperv->u.health_info.status;
	return cmd;
}

static struct ndctl_cmd *hyperv_dimm_cmd_new_smart(struct ndctl_dimm *dimm)
{
	return alloc_hyperv_cmd(dimm, ND_HYPERV_CMD_GET_HEALTH_INFO);
}

static int hyperv_cmd_valid(struct ndctl_cmd *cmd, unsigned int command)
{
	if (cmd->type != ND_CMD_CALL ||
	    cmd->size != sizeof(*cmd) + sizeof(struct nd_pkg_hyperv) ||
	    cmd->hyperv->gen.nd_family != NVDIMM_FAMILY_HYPERV ||
	    cmd->hyperv->gen.nd_command != command ||
	    cmd->status != 0 ||
	    cmd->hyperv->u.status != 0)
		return cmd->status < 0 ? cmd->status : -EINVAL;

	return 0;
}

static int hyperv_valid_health_info(struct ndctl_cmd *cmd)
{
	return hyperv_cmd_valid(cmd, ND_HYPERV_CMD_GET_HEALTH_INFO);
}

static unsigned int hyperv_cmd_get_flags(struct ndctl_cmd *cmd)
{
	unsigned int flags = 0;
	int rc;

	rc = hyperv_valid_health_info(cmd);
	if (rc < 0) {
		errno = -rc;
		return 0;
	}
	flags |= ND_SMART_HEALTH_VALID;

	return flags;
}

static unsigned int hyperv_cmd_get_health(struct ndctl_cmd *cmd)
{
	unsigned int health = 0;
	__u32 num;
	int rc;

	rc = hyperv_valid_health_info(cmd);
	if (rc < 0) {
		errno = -rc;
		return UINT_MAX;
	}

	num = cmd->hyperv->u.health_info.health & 0x3F;

	if (num & (BIT(0) | BIT(1)))
		health |= ND_SMART_CRITICAL_HEALTH;

	if (num & BIT(2))
		health |= ND_SMART_FATAL_HEALTH;

	if (num & (BIT(3) | BIT(4) | BIT(5)))
		health |= ND_SMART_NON_CRITICAL_HEALTH;

	return health;
}

static int hyperv_cmd_xlat_firmware_status(struct ndctl_cmd *cmd)
{
	return cmd->hyperv->u.status == 0 ? 0 : -EINVAL;
}

struct ndctl_dimm_ops * const hyperv_dimm_ops = &(struct ndctl_dimm_ops) {
	.new_smart = hyperv_dimm_cmd_new_smart,
	.smart_get_flags = hyperv_cmd_get_flags,
	.smart_get_health = hyperv_cmd_get_health,
	.xlat_firmware_status = hyperv_cmd_xlat_firmware_status,
};
