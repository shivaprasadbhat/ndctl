// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2015-2020 Intel Corporation. All rights reserved.
#include <linux/version.h>
#include <sys/utsname.h>
#include <libkmod.h>
#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include <test.h>

#include <util/log.h>
#include <util/sysfs.h>
#include <ndctl/libndctl.h>
#include <ndctl/ndctl.h>
#include <ccan/array_size/array_size.h>

#define KVER_STRLEN 20

struct ndctl_test {
	unsigned int kver;
	int attempt;
	int skip;
};

static unsigned int get_system_kver(void)
{
	const char *kver = getenv("KVER");
	struct utsname utsname;
	int a, b, c;

	if (!kver) {
		uname(&utsname);
		kver = utsname.release;
	}

	if (sscanf(kver, "%d.%d.%d", &a, &b, &c) != 3)
		return LINUX_VERSION_CODE;

	return KERNEL_VERSION(a,b,c);
}

struct ndctl_test *ndctl_test_new(unsigned int kver)
{
	struct ndctl_test *test = calloc(1, sizeof(*test));

	if (!test)
		return NULL;

	if (!kver)
		test->kver = get_system_kver();
	else
		test->kver = kver;

	return test;
}

int ndctl_test_result(struct ndctl_test *test, int rc)
{
	if (ndctl_test_get_skipped(test))
		fprintf(stderr, "attempted: %d skipped: %d\n",
				ndctl_test_get_attempted(test),
				ndctl_test_get_skipped(test));
	if (rc && rc != 77)
		return rc;
	if (ndctl_test_get_skipped(test) >= ndctl_test_get_attempted(test))
		return 77;
	/* return success if no failures and at least one test not skipped */
	return 0;
}

static char *kver_str(char *buf, unsigned int kver)
{
	snprintf(buf, KVER_STRLEN, "%d.%d.%d",  (kver >> 16) & 0xffff,
			(kver >> 8) & 0xff, kver & 0xff);
	return buf;
}

int __ndctl_test_attempt(struct ndctl_test *test, unsigned int kver,
		const char *caller, int line)
{
	char requires[KVER_STRLEN], current[KVER_STRLEN];

	test->attempt++;
	if (kver <= test->kver)
		return 1;
	fprintf(stderr, "%s: skip %s:%d requires: %s current: %s\n",
			__func__, caller, line, kver_str(requires, kver),
			kver_str(current, test->kver));
	test->skip++;
	return 0;
}

void __ndctl_test_skip(struct ndctl_test *test, const char *caller, int line)
{
	test->skip++;
	test->attempt = test->skip;
	fprintf(stderr, "%s: explicit skip %s:%d\n", __func__, caller, line);
}

int ndctl_test_get_attempted(struct ndctl_test *test)
{
	return test->attempt;
}

int ndctl_test_get_skipped(struct ndctl_test *test)
{
	return test->skip;
}

void ndctl_test_module_remove(struct kmod_ctx **ctx, struct kmod_module **mod,
				struct ndctl_ctx *nd_ctx)
{
	struct ndctl_bus *bus;
	int rc;

	ndctl_bus_foreach(nd_ctx, bus) {
		struct ndctl_region *region;

		if ((strcmp(ndctl_bus_get_provider(bus),
			   "nfit_test.0") != 0) &&
			strcmp(ndctl_bus_get_provider(bus),
				"nfit_test.1") != 0)
			continue;

		ndctl_region_foreach(bus, region)
			ndctl_region_disable_invalidate(region);
	}

	rc = kmod_module_remove_module(*mod, 0);
	if (rc < 0 && rc != -ENOENT) {
		fprintf(stderr, "couldn't remove module %s\n",
				    strerror(-rc));
	}

	kmod_unref(*ctx);
}

int ndctl_test_init(struct kmod_ctx **ctx, struct kmod_module **mod,
		struct ndctl_ctx *nd_ctx, int log_level,
		struct ndctl_test *test)
{
	int rc, family = -1;
	unsigned int i;
	const char *name;
	struct ndctl_bus *bus;
	struct log_ctx log_ctx;
	const char *list[] = {
		"nfit",
		"device_dax",
		"dax_pmem",
		"dax_pmem_compat",
		"libnvdimm",
		"nd_btt",
		"nd_e820",
		"nd_pmem",
	};
	char *test_env;

	log_init(&log_ctx, "test/init", "NDCTL_TEST");
	log_ctx.log_priority = log_level;

	/*
	 * The following two checks determine the platform family. For
	 * Intel/platforms which support ACPI, check sysfs; for other platforms
	 * determine from the environment variable NVDIMM_TEST_FAMILY
	 */
	if (access("/sys/bus/acpi", F_OK) == 0)
		family = NVDIMM_FAMILY_INTEL;

	test_env = getenv("NDCTL_TEST_FAMILY");
	if (test_env && strcmp(test_env, "PAPR") == 0)
		family = NVDIMM_FAMILY_PAPR;

	if (family == -1) {
		log_err(&log_ctx, "Cannot determine NVDIMM family\n");
		return -ENOTSUP;
	}

	*ctx = kmod_new(NULL, NULL);
	if (!*ctx)
		return -ENXIO;
	kmod_set_log_priority(*ctx, log_level);

	/*
	 * Check that all nfit, libnvdimm, and device-dax modules are
	 * the mocked versions. If they are loaded, check that they have
	 * the "out-of-tree" kernel taint, otherwise check that they
	 * come from the "/lib/modules/<KVER>/extra" directory.
	 */
	for (i = 0; i < ARRAY_SIZE(list); i++) {
		char attr[SYSFS_ATTR_SIZE];
		const char *path;
		char buf[100];
		int state;

		name = list[i];

		/*
		 * Don't check for device-dax modules on kernels older
		 * than 4.7.
		 */
		if (strcmp(name, "dax") == 0
				&& !ndctl_test_attempt(test,
					KERNEL_VERSION(4, 7, 0)))
			continue;

		/*
		 * Skip device-dax bus-model modules on pre-v5.1
		 */
		if ((strcmp(name, "dax_pmem_compat") == 0) &&
		    !ndctl_test_attempt(test, KERNEL_VERSION(5, 1, 0)))
			continue;

retry:
		rc = kmod_module_new_from_name(*ctx, name, mod);
		if (rc) {
			log_err(&log_ctx, "failed to interrogate %s.ko\n",
				name);
			break;
		}

		path = kmod_module_get_path(*mod);
		if (!path) {
			/*
			 * dax_pmem_compat is not required, missing is
			 * ok, present-but-production is not ok.
			 */
			if (strcmp(name, "dax_pmem_compat") == 0)
				continue;

			if (family != NVDIMM_FAMILY_INTEL &&
			    (strcmp(name, "nfit") == 0 ||
			     strcmp(name, "nd_e820") == 0))
				continue;

			log_err(&log_ctx, "%s.ko: failed to get path\n", name);
			break;
		}

		if (!strstr(path, "/extra/")) {
			log_err(&log_ctx, "%s.ko: appears to be production version: %s\n",
					name, path);
			break;
		}

		state = kmod_module_get_initstate(*mod);
		if (state == KMOD_MODULE_LIVE) {
			sprintf(buf, "/sys/module/%s/taint", name);
			rc = __sysfs_read_attr(&log_ctx, buf, attr);
			if (rc < 0) {
				log_err(&log_ctx, "%s.ko: failed to read %s\n",
						name, buf);
				break;
			}

			if (!strchr(attr, 'O')) {
				log_err(&log_ctx, "%s.ko: expected taint: O got: %s\n",
						name, attr);
				break;
			}
		} else if (state == KMOD_MODULE_BUILTIN) {
			log_err(&log_ctx, "%s: must be built as a module\n", name);
			break;
		}
	}

	if (i < ARRAY_SIZE(list)) {
		/* device-dax changed module names in 4.12 */
		if (strcmp(name, "device_dax") == 0) {
			name = "dax";
			goto retry;
		}
		kmod_unref(*ctx);
		return -ENXIO;
	}

	rc = kmod_module_new_from_name(*ctx, "nfit_test", mod);
	if (rc < 0) {
		kmod_unref(*ctx);
		return rc;
	}

	if (nd_ctx) {
		/* caller wants a full nfit_test reset */
		ndctl_bus_foreach(nd_ctx, bus) {
			struct ndctl_region *region;

			if (strcmp(ndctl_bus_get_provider(bus),
				   "nfit_test.0") != 0)
				continue;
			ndctl_region_foreach(bus, region)
				ndctl_region_disable_invalidate(region);
		}

		rc = kmod_module_remove_module(*mod, 0);
		if (rc < 0 && rc != -ENOENT) {
			kmod_unref(*ctx);
			return rc;
		}
		ndctl_invalidate(nd_ctx);
	}

	rc = kmod_module_probe_insert_module(*mod, KMOD_PROBE_APPLY_BLACKLIST,
			NULL, NULL, NULL, NULL);
	if (rc)
		kmod_unref(*ctx);

	if (!nd_ctx)
		return rc;

	ndctl_bus_foreach (nd_ctx, bus) {
		struct ndctl_region *region;
		struct ndctl_dimm *dimm;

		if (strcmp(ndctl_bus_get_provider(bus), "nfit_test.0") != 0)
			continue;

		ndctl_region_foreach (bus, region)
			ndctl_region_disable_invalidate(region);

		ndctl_dimm_foreach (bus, dimm) {
			ndctl_dimm_read_label_index(dimm);
			ndctl_dimm_init_labels(dimm, NDCTL_NS_VERSION_1_2);
			ndctl_dimm_disable(dimm);
			ndctl_dimm_enable(dimm);
		}

		ndctl_region_foreach (bus, region)
			ndctl_region_enable(region);
	}

	return 0;
}
