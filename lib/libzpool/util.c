// SPDX-License-Identifier: CDDL-1.0
/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or https://opensource.org/licenses/CDDL-1.0.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2016 by Delphix. All rights reserved.
 * Copyright 2017 Jason King
 * Copyright (c) 2017, Intel Corporation.
 */

#include <assert.h>
#include <sys/zfs_context.h>
#include <sys/avl.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/spa.h>
#include <sys/fs/zfs.h>
#include <sys/zfs_refcount.h>
#include <sys/zfs_ioctl.h>
#include <sys/tunables.h>
#include <libzutil.h>

/*
 * Routines needed by more than one client of libzpool.
 */

static void
show_vdev_stats(const char *desc, const char *ctype, nvlist_t *nv, int indent)
{
	vdev_stat_t *vs;
	vdev_stat_t *v0 = { 0 };
	uint64_t sec;
	uint64_t is_log = 0;
	nvlist_t **child;
	uint_t c, children;
	char used[6], avail[6];
	char rops[6], wops[6], rbytes[6], wbytes[6], rerr[6], werr[6], cerr[6];

	v0 = umem_zalloc(sizeof (*v0), UMEM_NOFAIL);

	if (indent == 0 && desc != NULL) {
		(void) printf("                           "
		    " capacity   operations   bandwidth  ---- errors ----\n");
		(void) printf("description                "
		    "used avail  read write  read write  read write cksum\n");
	}

	if (desc != NULL) {
		const char *suffix = "";
		const char *bias = NULL;
		char bias_suffix[32];

		(void) nvlist_lookup_uint64(nv, ZPOOL_CONFIG_IS_LOG, &is_log);
		(void) nvlist_lookup_string(nv, ZPOOL_CONFIG_ALLOCATION_BIAS,
		    &bias);
		if (nvlist_lookup_uint64_array(nv, ZPOOL_CONFIG_VDEV_STATS,
		    (uint64_t **)&vs, &c) != 0)
			vs = v0;

		if (bias != NULL) {
			(void) snprintf(bias_suffix, sizeof (bias_suffix),
			    " (%s)", bias);
			suffix = bias_suffix;
		} else if (is_log) {
			suffix = " (log)";
		}

		sec = MAX(1, vs->vs_timestamp / NANOSEC);

		nicenum(vs->vs_alloc, used, sizeof (used));
		nicenum(vs->vs_space - vs->vs_alloc, avail, sizeof (avail));
		nicenum(vs->vs_ops[ZIO_TYPE_READ] / sec, rops, sizeof (rops));
		nicenum(vs->vs_ops[ZIO_TYPE_WRITE] / sec, wops, sizeof (wops));
		nicenum(vs->vs_bytes[ZIO_TYPE_READ] / sec, rbytes,
		    sizeof (rbytes));
		nicenum(vs->vs_bytes[ZIO_TYPE_WRITE] / sec, wbytes,
		    sizeof (wbytes));
		nicenum(vs->vs_read_errors, rerr, sizeof (rerr));
		nicenum(vs->vs_write_errors, werr, sizeof (werr));
		nicenum(vs->vs_checksum_errors, cerr, sizeof (cerr));

		(void) printf("%*s%s%*s%*s%*s %5s %5s %5s %5s %5s %5s %5s\n",
		    indent, "",
		    desc,
		    (int)(indent+strlen(desc)-25-(vs->vs_space ? 0 : 12)),
		    suffix,
		    vs->vs_space ? 6 : 0, vs->vs_space ? used : "",
		    vs->vs_space ? 6 : 0, vs->vs_space ? avail : "",
		    rops, wops, rbytes, wbytes, rerr, werr, cerr);
	}
	umem_free(v0, sizeof (*v0));

	if (nvlist_lookup_nvlist_array(nv, ctype, &child, &children) != 0)
		return;

	for (c = 0; c < children; c++) {
		nvlist_t *cnv = child[c];
		const char *cname = NULL;
		char *tname;
		uint64_t np;
		int len;
		if (nvlist_lookup_string(cnv, ZPOOL_CONFIG_PATH, &cname) &&
		    nvlist_lookup_string(cnv, ZPOOL_CONFIG_TYPE, &cname))
			cname = "<unknown>";
		len = strlen(cname) + 2;
		tname = umem_zalloc(len, UMEM_NOFAIL);
		(void) strlcpy(tname, cname, len);
		if (nvlist_lookup_uint64(cnv, ZPOOL_CONFIG_NPARITY, &np) == 0)
			tname[strlen(tname)] = '0' + np;
		show_vdev_stats(tname, ctype, cnv, indent + 2);
		umem_free(tname, len);
	}
}

void
show_pool_stats(spa_t *spa)
{
	nvlist_t *config, *nvroot;
	const char *name;

	VERIFY(spa_get_stats(spa_name(spa), &config, NULL, 0) == 0);

	VERIFY(nvlist_lookup_nvlist(config, ZPOOL_CONFIG_VDEV_TREE,
	    &nvroot) == 0);
	VERIFY(nvlist_lookup_string(config, ZPOOL_CONFIG_POOL_NAME,
	    &name) == 0);

	show_vdev_stats(name, ZPOOL_CONFIG_CHILDREN, nvroot, 0);
	show_vdev_stats(NULL, ZPOOL_CONFIG_L2CACHE, nvroot, 0);
	show_vdev_stats(NULL, ZPOOL_CONFIG_SPARES, nvroot, 0);

	nvlist_free(config);
}

/*
 * Common helper for working with libzpool tunables from the command line.
 *
 * Valid inputs:
 *
 *   <name>		show named tunable and value
 *   <name>=<value>	set tunable value
 *
 *   show		show all tunables and values
 *   show=<name>	show named tunable and value
 *   info		show info about all tunables
 *   info=<name>	show info about named tunable
 */

typedef enum { SHOW, INFO, SET } tunable_mode_t;

static int
list_tunables_cb(const zfs_tunable_t *tunable, void *arg)
{
	const tunable_mode_t *mode = arg;

	static const char *type[] = {
		"int", "uint", "ulong", "u64", "str",
	};
	static const char *perm[] = {
		"rw", "rd",
	};

	if (*mode == SHOW) {
		char val[64];
		int err = zfs_tunable_get(tunable, val, sizeof (val));
		if (err == 0)
			printf("%s: %s\n", tunable->zt_name, val);
		else
			printf("%s: [error getting tunable value: %s]\n",
			    tunable->zt_name, strerror(err));
	} else {
		printf("%s [%s %s]: %s\n", tunable->zt_name,
		    type[tunable->zt_type], perm[tunable->zt_perm],
		    tunable->zt_desc);
	}

	return (0);
}
int
handle_tunable_option(const char *_arg, boolean_t quiet)
{
	int err = 0;
	char *arg = strdup(_arg);
	char *k, *v;

	v = arg;
	k = strsep(&v, "=");

	tunable_mode_t mode;

	if (strcmp(k, "show") == 0) {
		mode = SHOW;
		k = v;
	} else if (strcmp(k, "info") == 0) {
		mode = INFO;
		k = v;
	} else if (v == NULL) {
		mode = SHOW;
	} else {
		mode = SET;
	}

	if (quiet && mode != SET) {
		err = EINVAL;
		goto out;
	}

	if (mode == SET) {
		const zfs_tunable_t *tunable = zfs_tunable_lookup(k);
		if (tunable == NULL) {
			err = ENOENT;
			goto out;
		}

		char vold[256], vnew[256];
		if (zfs_tunable_get(tunable, vold, sizeof (vold)) != 0)
			strcpy(vold, "???");
		err = zfs_tunable_set(tunable, v);
		if (err != 0)
			goto out;
		if (zfs_tunable_get(tunable, vnew, sizeof (vnew)) != 0)
			strcpy(vnew, "???");

		if (!quiet)
			printf("%s: %s -> %s\n", k, vold, vnew);
	} else if (k != NULL) {
		const zfs_tunable_t *tunable = zfs_tunable_lookup(k);
		if (tunable == NULL) {
			err = ENOENT;
			goto out;
		}
		list_tunables_cb(tunable, &mode);
	} else {
		zfs_tunable_iter(list_tunables_cb, &mode);
	}

out:
	if (!quiet) {
		if (err == ENOENT)
			fprintf(stderr, "no such tunable: %s\n", k);
		else if (err != 0)
			fprintf(stderr, "couldn't set tunable '%s': %s\n",
			    k, strerror(err));
	}

	free(arg);
	return (err);
}

static nvlist_t *
refresh_config(void *unused, nvlist_t *tryconfig)
{
	(void) unused;
	return (spa_tryimport(tryconfig));
}

#if defined(__FreeBSD__)

#include <sys/param.h>
#include <sys/sysctl.h>
#include <os/freebsd/zfs/sys/zfs_ioctl_compat.h>

static int
pool_active(void *unused, const char *name, uint64_t guid, boolean_t *isactive)
{
	(void) unused, (void) guid;
	zfs_iocparm_t zp;
	zfs_cmd_t *zc = NULL;
#ifdef ZFS_LEGACY_SUPPORT
	zfs_cmd_legacy_t *zcl = NULL;
#endif
	unsigned long request;
	int ret;

	int fd = open(ZFS_DEV, O_RDWR | O_CLOEXEC);
	if (fd < 0)
		return (-1);

	/*
	 * Use ZFS_IOC_POOL_STATS to check if the pool is active.  We want to
	 * avoid adding a dependency on libzfs_core solely for this ioctl(),
	 * therefore we manually craft the stats command.  Note that the command
	 * ID is identical between the openzfs and legacy ioctl() formats.
	 */
	int ver = ZFS_IOCVER_NONE;
	size_t ver_size = sizeof (ver);

	sysctlbyname("vfs.zfs.version.ioctl", &ver, &ver_size, NULL, 0);

	switch (ver) {
	case ZFS_IOCVER_OZFS:
		zc = umem_zalloc(sizeof (zfs_cmd_t), UMEM_NOFAIL);

		(void) strlcpy(zc->zc_name, name, sizeof (zc->zc_name));
		zp.zfs_cmd = (uint64_t)(uintptr_t)zc;
		zp.zfs_cmd_size = sizeof (zfs_cmd_t);
		zp.zfs_ioctl_version = ZFS_IOCVER_OZFS;

		request = _IOWR('Z', ZFS_IOC_POOL_STATS, zfs_iocparm_t);
		ret = ioctl(fd, request, &zp);

		free((void *)(uintptr_t)zc->zc_nvlist_dst);
		umem_free(zc, sizeof (zfs_cmd_t));

		break;
#ifdef ZFS_LEGACY_SUPPORT
	case ZFS_IOCVER_LEGACY:
		zcl = umem_zalloc(sizeof (zfs_cmd_legacy_t), UMEM_NOFAIL);

		(void) strlcpy(zcl->zc_name, name, sizeof (zcl->zc_name));
		zp.zfs_cmd = (uint64_t)(uintptr_t)zcl;
		zp.zfs_cmd_size = sizeof (zfs_cmd_legacy_t);
		zp.zfs_ioctl_version = ZFS_IOCVER_LEGACY;

		request = _IOWR('Z', ZFS_IOC_POOL_STATS, zfs_iocparm_t);
		ret = ioctl(fd, request, &zp);

		free((void *)(uintptr_t)zcl->zc_nvlist_dst);
		umem_free(zcl, sizeof (zfs_cmd_legacy_t));

		break;
#endif
	default:
		fprintf(stderr, "unrecognized zfs ioctl version %d", ver);
		exit(1);
	}

	(void) close(fd);

	*isactive = (ret == 0);

	return (0);
}
#else
static int
pool_active(void *unused, const char *name, uint64_t guid,
    boolean_t *isactive)
{
	(void) unused, (void) guid;
	int fd = open(ZFS_DEV, O_RDWR | O_CLOEXEC);
	if (fd < 0)
		return (-1);

	/*
	 * Use ZFS_IOC_POOL_STATS to check if a pool is active.
	 */
	zfs_cmd_t *zcp = umem_zalloc(sizeof (zfs_cmd_t), UMEM_NOFAIL);
	(void) strlcpy(zcp->zc_name, name, sizeof (zcp->zc_name));

	int ret = ioctl(fd, ZFS_IOC_POOL_STATS, zcp);

	free((void *)(uintptr_t)zcp->zc_nvlist_dst);
	umem_free(zcp, sizeof (zfs_cmd_t));

	(void) close(fd);

	*isactive = (ret == 0);

	return (0);
}
#endif

pool_config_ops_t libzpool_config_ops = {
	.pco_refresh_config = refresh_config,
	.pco_pool_active = pool_active,
};
