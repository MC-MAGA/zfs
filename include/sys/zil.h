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
 * Copyright (c) 2012, 2018 by Delphix. All rights reserved.
 */

/* Portions Copyright 2010 Robert Milkowski */

#ifndef	_SYS_ZIL_H
#define	_SYS_ZIL_H

#include <sys/types.h>
#include <sys/spa.h>
#include <sys/zio.h>
#include <sys/dmu.h>
#include <sys/zio_crypt.h>
#include <sys/wmsum.h>

#ifdef	__cplusplus
extern "C" {
#endif

struct dsl_pool;
struct dsl_dataset;
struct lwb;

/*
 * Intent log format:
 *
 * Each objset has its own intent log.  The log header (zil_header_t)
 * for objset N's intent log is kept in the Nth object of the SPA's
 * intent_log objset.  The log header points to a chain of log blocks,
 * each of which contains log records (i.e., transactions) followed by
 * a log block trailer (zil_trailer_t).  The format of a log record
 * depends on the record (or transaction) type, but all records begin
 * with a common structure that defines the type, length, and txg.
 */

/*
 * Intent log header - this on disk structure holds fields to manage
 * the log.  All fields are 64 bit to easily handle cross architectures.
 */
typedef struct zil_header {
	uint64_t zh_claim_txg;	/* txg in which log blocks were claimed */
	uint64_t zh_replay_seq;	/* highest replayed sequence number */
	blkptr_t zh_log;	/* log chain */
	uint64_t zh_claim_blk_seq; /* highest claimed block sequence number */
	uint64_t zh_flags;	/* header flags */
	uint64_t zh_claim_lr_seq; /* highest claimed lr sequence number */
	uint64_t zh_pad[3];
} zil_header_t;

/*
 * zh_flags bit settings
 */
#define	ZIL_REPLAY_NEEDED	0x1	/* replay needed - internal only */
#define	ZIL_CLAIM_LR_SEQ_VALID	0x2	/* zh_claim_lr_seq field is valid */

/*
 * Log block chaining.
 *
 * Log blocks are chained together. Originally they were chained at the
 * end of the block. For performance reasons the chain was moved to the
 * beginning of the block which allows writes for only the data being used.
 * The older position is supported for backwards compatibility.
 *
 * The zio_eck_t contains a zec_cksum which for the intent log is
 * the sequence number of this log block. A seq of 0 is invalid.
 * The zec_cksum is checked by the SPA against the sequence
 * number passed in the blk_cksum field of the blkptr_t
 */
typedef struct zil_chain {
	uint64_t zc_pad;
	blkptr_t zc_next_blk;	/* next block in chain */
	uint64_t zc_nused;	/* bytes in log block used */
	zio_eck_t zc_eck;	/* block trailer */
} zil_chain_t;

#define	ZIL_MIN_BLKSZ	4096ULL

/*
 * ziltest is by and large an ugly hack, but very useful in
 * checking replay without tedious work.
 * When running ziltest we want to keep all itx's and so maintain
 * a single list in the zl_itxg[] that uses a high txg: ZILTEST_TXG
 * We subtract TXG_CONCURRENT_STATES to allow for common code.
 */
#define	ZILTEST_TXG (UINT64_MAX - TXG_CONCURRENT_STATES)

/*
 * The words of a log block checksum.
 */
#define	ZIL_ZC_GUID_0	0
#define	ZIL_ZC_GUID_1	1
#define	ZIL_ZC_OBJSET	2
#define	ZIL_ZC_SEQ	3

typedef enum zil_create {
	Z_FILE,
	Z_DIR,
	Z_XATTRDIR,
} zil_create_t;

/*
 * size of xvattr log section.
 * its composed of lr_attr_t + xvattr bitmap + 2 64 bit timestamps
 * for create time and a single 64 bit integer for all of the attributes,
 * and 4 64 bit integers (32 bytes) for the scanstamp.
 *
 */

#define	ZIL_XVAT_SIZE(mapsize) \
	sizeof (lr_attr_t) + (sizeof (uint32_t) * (mapsize - 1)) + \
	(sizeof (uint64_t) * 7)

/*
 * Size of ACL in log.  The ACE data is padded out to properly align
 * on 8 byte boundary.
 */

#define	ZIL_ACE_LENGTH(x)	(roundup(x, sizeof (uint64_t)))

/*
 * Intent log transaction types and record structures
 */
#define	TX_COMMIT		0	/* Commit marker (no on-disk state) */
#define	TX_CREATE		1	/* Create file */
#define	TX_MKDIR		2	/* Make directory */
#define	TX_MKXATTR		3	/* Make XATTR directory */
#define	TX_SYMLINK		4	/* Create symbolic link to a file */
#define	TX_REMOVE		5	/* Remove file */
#define	TX_RMDIR		6	/* Remove directory */
#define	TX_LINK			7	/* Create hard link to a file */
#define	TX_RENAME		8	/* Rename a file */
#define	TX_WRITE		9	/* File write */
#define	TX_TRUNCATE		10	/* Truncate a file */
#define	TX_SETATTR		11	/* Set file attributes */
#define	TX_ACL_V0		12	/* Set old formatted ACL */
#define	TX_ACL			13	/* Set ACL */
#define	TX_CREATE_ACL		14	/* create with ACL */
#define	TX_CREATE_ATTR		15	/* create + attrs */
#define	TX_CREATE_ACL_ATTR 	16	/* create with ACL + attrs */
#define	TX_MKDIR_ACL		17	/* mkdir with ACL */
#define	TX_MKDIR_ATTR		18	/* mkdir with attr */
#define	TX_MKDIR_ACL_ATTR	19	/* mkdir with ACL + attrs */
#define	TX_WRITE2		20	/* dmu_sync EALREADY write */
#define	TX_SETSAXATTR		21	/* Set sa xattrs on file */
#define	TX_RENAME_EXCHANGE	22	/* Atomic swap via renameat2 */
#define	TX_RENAME_WHITEOUT	23	/* Atomic whiteout via renameat2 */
#define	TX_CLONE_RANGE		24	/* Clone a file range */
#define	TX_MAX_TYPE		25	/* Max transaction type */

/*
 * The transactions for mkdir, symlink, remove, rmdir, link, and rename
 * may have the following bit set, indicating the original request
 * specified case-insensitive handling of names.
 */
#define	TX_CI	((uint64_t)0x1 << 63) /* case-insensitive behavior requested */

/*
 * Transactions for operations below can be logged out of order.
 * For convenience in the code, all such records must have lr_foid
 * at the same offset.
 */
#define	TX_OOO(txtype)			\
	((txtype) == TX_WRITE ||	\
	(txtype) == TX_TRUNCATE ||	\
	(txtype) == TX_SETATTR ||	\
	(txtype) == TX_ACL_V0 ||	\
	(txtype) == TX_ACL ||		\
	(txtype) == TX_WRITE2 ||	\
	(txtype) == TX_SETSAXATTR ||	\
	(txtype) == TX_CLONE_RANGE)

/*
 * The number of dnode slots consumed by the object is stored in the 8
 * unused upper bits of the object ID. We subtract 1 from the value
 * stored on disk for compatibility with implementations that don't
 * support large dnodes. The slot count for a single-slot dnode will
 * contain 0 for those bits to preserve the log record format for
 * "small" dnodes.
 */
#define	LR_FOID_GET_SLOTS(oid) (BF64_GET((oid), 56, 8) + 1)
#define	LR_FOID_SET_SLOTS(oid, x) BF64_SET((oid), 56, 8, (x) - 1)
#define	LR_FOID_GET_OBJ(oid) BF64_GET((oid), 0, DN_MAX_OBJECT_SHIFT)
#define	LR_FOID_SET_OBJ(oid, x) BF64_SET((oid), 0, DN_MAX_OBJECT_SHIFT, (x))

/*
 * Format of log records.
 * The fields are carefully defined to allow them to be aligned
 * and sized the same on sparc & intel architectures.
 * Each log record has a common structure at the beginning.
 *
 * The log record on disk (lrc_seq) holds the sequence number of all log
 * records which is used to ensure we don't replay the same record.
 */
typedef struct {			/* common log record header */
	uint64_t	lrc_txtype;	/* intent log transaction type */
	uint64_t	lrc_reclen;	/* transaction record length */
	uint64_t	lrc_txg;	/* dmu transaction group number */
	uint64_t	lrc_seq;	/* see comment above */
} lr_t;

/*
 * Common start of all out-of-order record types (TX_OOO() above).
 */
typedef struct {
	lr_t		lr_common;	/* common portion of log record */
	uint64_t	lr_foid;	/* object id */
} lr_ooo_t;

/*
 * Additional lr_attr_t fields.
 */
typedef struct {
	uint64_t	lr_attr_attrs;		/* all of the attributes */
	uint64_t	lr_attr_crtime[2];	/* create time */
	uint8_t		lr_attr_scanstamp[32];
} lr_attr_end_t;

/*
 * Handle option extended vattr attributes.
 *
 * Whenever new attributes are added the version number
 * will need to be updated as will code in
 * zfs_log.c and zfs_replay.c
 */
typedef struct {
	uint32_t	lr_attr_masksize; /* number of elements in array */
	uint32_t	lr_attr_bitmap; /* First entry of array */
	/* remainder of array and additional lr_attr_end_t fields */
	uint8_t		lr_attr_data[];
} lr_attr_t;

/*
 * log record for creates without optional ACL.
 * This log record does support optional xvattr_t attributes.
 */
typedef struct {
	lr_t		lr_common;	/* common portion of log record */
	uint64_t	lr_doid;	/* object id of directory */
	uint64_t	lr_foid;	/* object id of created file object */
	uint64_t	lr_mode;	/* mode of object */
	uint64_t	lr_uid;		/* uid of object */
	uint64_t	lr_gid;		/* gid of object */
	uint64_t	lr_gen;		/* generation (txg of creation) */
	uint64_t	lr_crtime[2];	/* creation time */
	uint64_t	lr_rdev;	/* rdev of object to create */
} _lr_create_t;

typedef struct {
	_lr_create_t	lr_create;	/* common create portion */
	/* name of object to create follows this */
	/* for symlinks, link content follows name */
	/* for creates with xvattr data, the name follows the xvattr info */
	uint8_t		lr_data[];
} lr_create_t;

/*
 * FUID ACL record will be an array of ACEs from the original ACL.
 * If this array includes ephemeral IDs, the record will also include
 * an array of log-specific FUIDs to replace the ephemeral IDs.
 * Only one copy of each unique domain will be present, so the log-specific
 * FUIDs will use an index into a compressed domain table.  On replay this
 * information will be used to construct real FUIDs (and bypass idmap,
 * since it may not be available).
 */

/*
 * Log record for creates with optional ACL
 * This log record is also used for recording any FUID
 * information needed for replaying the create.  If the
 * file doesn't have any actual ACEs then the lr_aclcnt
 * would be zero.
 *
 * After lr_acl_flags, there are a lr_acl_bytes number of variable sized ace's.
 * If create is also setting xvattr's, then acl data follows xvattr.
 * If ACE FUIDs are needed then they will follow the xvattr_t.  Following
 * the FUIDs will be the domain table information.  The FUIDs for the owner
 * and group will be in lr_create.  Name follows ACL data.
 */
typedef struct {
	_lr_create_t	lr_create;	/* common create portion */
	uint64_t	lr_aclcnt;	/* number of ACEs in ACL */
	uint64_t	lr_domcnt;	/* number of unique domains */
	uint64_t	lr_fuidcnt;	/* number of real fuids */
	uint64_t	lr_acl_bytes;	/* number of bytes in ACL */
	uint64_t	lr_acl_flags;	/* ACL flags */
	uint8_t		lr_data[];
} lr_acl_create_t;

typedef struct {
	lr_t		lr_common;	/* common portion of log record */
	uint64_t	lr_doid;	/* obj id of directory */
	/* name of object to remove follows this */
	uint8_t		lr_data[];
} lr_remove_t;

typedef struct {
	lr_t		lr_common;	/* common portion of log record */
	uint64_t	lr_doid;	/* obj id of directory */
	uint64_t	lr_link_obj;	/* obj id of link */
	/* name of object to link follows this */
	uint8_t		lr_data[];
} lr_link_t;

typedef struct {
	lr_t		lr_common;	/* common portion of log record */
	uint64_t	lr_sdoid;	/* obj id of source directory */
	uint64_t	lr_tdoid;	/* obj id of target directory */
} _lr_rename_t;

typedef struct {
	_lr_rename_t	lr_rename;	/* common rename portion */
	/* 2 strings: names of source and destination follow this */
	uint8_t		lr_data[];
} lr_rename_t;

typedef struct {
	_lr_rename_t	lr_rename;	/* common rename portion */
	/* members related to the whiteout file (based on _lr_create_t) */
	uint64_t	lr_wfoid;	/* obj id of the new whiteout file */
	uint64_t	lr_wmode;	/* mode of object */
	uint64_t	lr_wuid;	/* uid of whiteout */
	uint64_t	lr_wgid;	/* gid of whiteout */
	uint64_t	lr_wgen;	/* generation (txg of creation) */
	uint64_t	lr_wcrtime[2];	/* creation time */
	uint64_t	lr_wrdev;	/* always makedev(0, 0) */
	/* 2 strings: names of source and destination follow this */
	uint8_t		lr_data[];
} lr_rename_whiteout_t;

typedef struct {
	lr_t		lr_common;	/* common portion of log record */
	uint64_t	lr_foid;	/* file object to write */
	uint64_t	lr_offset;	/* offset to write to */
	uint64_t	lr_length;	/* user data length to write */
	uint64_t	lr_blkoff;	/* no longer used */
	blkptr_t	lr_blkptr;	/* spa block pointer for replay */
	/* write data will follow for small writes */
	uint8_t		lr_data[];
} lr_write_t;

typedef struct {
	lr_t		lr_common;	/* common portion of log record */
	uint64_t	lr_foid;	/* object id of file to truncate */
	uint64_t	lr_offset;	/* offset to truncate from */
	uint64_t	lr_length;	/* length to truncate */
} lr_truncate_t;

typedef struct {
	lr_t		lr_common;	/* common portion of log record */
	uint64_t	lr_foid;	/* file object to change attributes */
	uint64_t	lr_mask;	/* mask of attributes to set */
	uint64_t	lr_mode;	/* mode to set */
	uint64_t	lr_uid;		/* uid to set */
	uint64_t	lr_gid;		/* gid to set */
	uint64_t	lr_size;	/* size to set */
	uint64_t	lr_atime[2];	/* access time */
	uint64_t	lr_mtime[2];	/* modification time */
	/* optional attribute lr_attr_t may be here */
	uint8_t		lr_data[];
} lr_setattr_t;

typedef struct {
	lr_t		lr_common;	/* common portion of log record */
	uint64_t	lr_foid;	/* file object to change attributes */
	uint64_t	lr_size;
	/* xattr name and value follows */
	uint8_t		lr_data[];
} lr_setsaxattr_t;

typedef struct {
	lr_t		lr_common;	/* common portion of log record */
	uint64_t	lr_foid;	/* obj id of file */
	uint64_t	lr_aclcnt;	/* number of acl entries */
	/* lr_aclcnt number of ace_t entries follow this */
	uint8_t		lr_data[];
} lr_acl_v0_t;

typedef struct {
	lr_t		lr_common;	/* common portion of log record */
	uint64_t	lr_foid;	/* obj id of file */
	uint64_t	lr_aclcnt;	/* number of ACEs in ACL */
	uint64_t	lr_domcnt;	/* number of unique domains */
	uint64_t	lr_fuidcnt;	/* number of real fuids */
	uint64_t	lr_acl_bytes;	/* number of bytes in ACL */
	uint64_t	lr_acl_flags;	/* ACL flags */
	/* lr_acl_bytes number of variable sized ace's follows */
	uint8_t		lr_data[];
} lr_acl_t;

typedef struct {
	lr_t		lr_common;	/* common portion of log record */
	uint64_t	lr_foid;	/* file object to clone into */
	uint64_t	lr_offset;	/* offset to clone to */
	uint64_t	lr_length;	/* length of the blocks to clone */
	uint64_t	lr_blksz;	/* file's block size */
	uint64_t	lr_nbps;	/* number of block pointers */
	/* block pointers of the blocks to clone follows */
	blkptr_t	lr_bps[];
} lr_clone_range_t;

/*
 * ZIL structure definitions, interface function prototype and globals.
 */

/*
 * Writes are handled in three different ways:
 *
 * WR_INDIRECT:
 *    In this mode, if we need to commit the write later, then the block
 *    is immediately written into the file system (using dmu_sync),
 *    and a pointer to the block is put into the log record.
 *    When the txg commits the block is linked in.
 *    This saves additionally writing the data into the log record.
 *    There are a few requirements for this to occur:
 *	- write is greater than zfs/zvol_immediate_write_sz
 *	- not using slogs (as slogs are assumed to always be faster
 *	  than writing into the main pool)
 *	- the write occupies only one block
 * WR_COPIED:
 *    If we know we'll immediately be committing the
 *    transaction (O_SYNC or O_DSYNC), then we allocate a larger
 *    log record here for the data and copy the data in.
 * WR_NEED_COPY:
 *    Otherwise we don't allocate a buffer, and *if* we need to
 *    flush the write later then a buffer is allocated and
 *    we retrieve the data using the dmu.
 */
typedef enum {
	WR_INDIRECT,	/* indirect - a large write (dmu_sync() data */
			/* and put blkptr in log, rather than actual data) */
	WR_COPIED,	/* immediate - data is copied into lr_write_t */
	WR_NEED_COPY,	/* immediate - data needs to be copied if pushed */
	WR_NUM_STATES	/* number of states */
} itx_wr_state_t;

typedef void (*zil_callback_t)(void *data);

typedef struct itx {
	list_node_t	itx_node;	/* linkage on zl_itx_list */
	void		*itx_private;	/* type-specific opaque data */
	itx_wr_state_t	itx_wr_state;	/* write state */
	uint8_t		itx_sync;	/* synchronous transaction */
	zil_callback_t	itx_callback;   /* Called when the itx is persistent */
	void		*itx_callback_data; /* User data for the callback */
	size_t		itx_size;	/* allocated itx structure size */
	uint64_t	itx_oid;	/* object id */
	uint64_t	itx_gen;	/* gen number for zfs_get_data */
	lr_t		itx_lr;		/* common part of log record */
	uint8_t		itx_lr_data[];	/* type-specific part of lr_xx_t */
} itx_t;

/*
 * Used for zil kstat.
 */
typedef struct zil_stats {
	/*
	 * Number of times a ZIL commit (e.g. fsync) has been requested.
	 */
	kstat_named_t zil_commit_count;

	/*
	 * Number of times the ZIL has been flushed to stable storage.
	 * This is less than zil_commit_count when commits are "merged"
	 * (see the documentation above zil_commit()).
	 */
	kstat_named_t zil_commit_writer_count;

	/*
	 * Number of times a ZIL commit failed and the ZIL was forced to fall
	 * back to txg_wait_synced(). The separate counts are for different
	 * reasons:
	 * -   error: ZIL IO (write/flush) returned an error
	 *            (see zil_commit_impl())
	 * -   stall: LWB block allocation failed, ZIL chain abandoned
	 *            (see zil_commit_writer_stall())
	 * - suspend: ZIL suspended
	 *            (see zil_commit(), zil_get_commit_list())
	 */
	kstat_named_t zil_commit_error_count;
	kstat_named_t zil_commit_stall_count;
	kstat_named_t zil_commit_suspend_count;

	/*
	 * Number of transactions (reads, writes, renames, etc.)
	 * that have been committed.
	 */
	kstat_named_t zil_itx_count;

	/*
	 * See the documentation for itx_wr_state_t above.
	 * Note that "bytes" accumulates the length of the transactions
	 * (i.e. data), not the actual log record sizes.
	 */
	kstat_named_t zil_itx_indirect_count;
	kstat_named_t zil_itx_indirect_bytes;
	kstat_named_t zil_itx_copied_count;
	kstat_named_t zil_itx_copied_bytes;
	kstat_named_t zil_itx_needcopy_count;
	kstat_named_t zil_itx_needcopy_bytes;

	/*
	 * Transactions which have been allocated to the "normal"
	 * (i.e. not slog) storage pool. Note that "bytes" accumulate
	 * the actual log record sizes - which do not include the actual
	 * data in case of indirect writes.  bytes <= write <= alloc.
	 */
	kstat_named_t zil_itx_metaslab_normal_count;
	kstat_named_t zil_itx_metaslab_normal_bytes;
	kstat_named_t zil_itx_metaslab_normal_write;
	kstat_named_t zil_itx_metaslab_normal_alloc;

	/*
	 * Transactions which have been allocated to the "slog" storage pool.
	 * If there are no separate log devices, this is the same as the
	 * "normal" pool.  bytes <= write <= alloc.
	 */
	kstat_named_t zil_itx_metaslab_slog_count;
	kstat_named_t zil_itx_metaslab_slog_bytes;
	kstat_named_t zil_itx_metaslab_slog_write;
	kstat_named_t zil_itx_metaslab_slog_alloc;
} zil_kstat_values_t;

typedef struct zil_sums {
	wmsum_t zil_commit_count;
	wmsum_t zil_commit_writer_count;
	wmsum_t zil_commit_error_count;
	wmsum_t zil_commit_stall_count;
	wmsum_t zil_commit_suspend_count;
	wmsum_t zil_itx_count;
	wmsum_t zil_itx_indirect_count;
	wmsum_t zil_itx_indirect_bytes;
	wmsum_t zil_itx_copied_count;
	wmsum_t zil_itx_copied_bytes;
	wmsum_t zil_itx_needcopy_count;
	wmsum_t zil_itx_needcopy_bytes;
	wmsum_t zil_itx_metaslab_normal_count;
	wmsum_t zil_itx_metaslab_normal_bytes;
	wmsum_t zil_itx_metaslab_normal_write;
	wmsum_t zil_itx_metaslab_normal_alloc;
	wmsum_t zil_itx_metaslab_slog_count;
	wmsum_t zil_itx_metaslab_slog_bytes;
	wmsum_t zil_itx_metaslab_slog_write;
	wmsum_t zil_itx_metaslab_slog_alloc;
} zil_sums_t;

#define	ZIL_STAT_INCR(zil, stat, val) \
	do { \
		int64_t tmpval = (val); \
		wmsum_add(&(zil_sums_global.stat), tmpval); \
		if ((zil)->zl_sums) \
			wmsum_add(&((zil)->zl_sums->stat), tmpval); \
	} while (0)

#define	ZIL_STAT_BUMP(zil, stat) \
    ZIL_STAT_INCR(zil, stat, 1);

typedef int zil_parse_blk_func_t(zilog_t *zilog, const blkptr_t *bp, void *arg,
    uint64_t txg);
typedef int zil_parse_lr_func_t(zilog_t *zilog, const lr_t *lr, void *arg,
    uint64_t txg);
typedef int zil_replay_func_t(void *arg1, void *arg2, boolean_t byteswap);
typedef int zil_get_data_t(void *arg, uint64_t arg2, lr_write_t *lr, char *dbuf,
    struct lwb *lwb, zio_t *zio);

extern int zil_parse(zilog_t *zilog, zil_parse_blk_func_t *parse_blk_func,
    zil_parse_lr_func_t *parse_lr_func, void *arg, uint64_t txg,
    boolean_t decrypt);

extern void	zil_init(void);
extern void	zil_fini(void);

extern zilog_t	*zil_alloc(objset_t *os, zil_header_t *zh_phys);
extern void	zil_free(zilog_t *zilog);

extern zilog_t	*zil_open(objset_t *os, zil_get_data_t *get_data,
    zil_sums_t *zil_sums);
extern void	zil_close(zilog_t *zilog);

extern boolean_t zil_replay(objset_t *os, void *arg,
    zil_replay_func_t *const replay_func[TX_MAX_TYPE]);
extern boolean_t zil_replaying(zilog_t *zilog, dmu_tx_t *tx);
extern boolean_t zil_destroy(zilog_t *zilog, boolean_t keep_first);
extern void	zil_destroy_sync(zilog_t *zilog, dmu_tx_t *tx);

extern itx_t	*zil_itx_create(uint64_t txtype, size_t lrsize);
extern void	zil_itx_destroy(itx_t *itx);
extern void	zil_itx_assign(zilog_t *zilog, itx_t *itx, dmu_tx_t *tx);

extern void	zil_async_to_sync(zilog_t *zilog, uint64_t oid);
extern void	zil_commit(zilog_t *zilog, uint64_t oid);
extern void	zil_commit_impl(zilog_t *zilog, uint64_t oid);
extern void	zil_remove_async(zilog_t *zilog, uint64_t oid);

extern int	zil_reset(const char *osname, void *txarg);
extern int	zil_claim(struct dsl_pool *dp,
    struct dsl_dataset *ds, void *txarg);
extern int 	zil_check_log_chain(struct dsl_pool *dp,
    struct dsl_dataset *ds, void *tx);
extern void	zil_sync(zilog_t *zilog, dmu_tx_t *tx);
extern void	zil_clean(zilog_t *zilog, uint64_t synced_txg);

extern int	zil_suspend(const char *osname, void **cookiep);
extern void	zil_resume(void *cookie);

extern void	zil_lwb_add_block(struct lwb *lwb, const blkptr_t *bp);
extern void	zil_lwb_add_txg(struct lwb *lwb, uint64_t txg);
extern int	zil_bp_tree_add(zilog_t *zilog, const blkptr_t *bp);

extern void	zil_set_sync(zilog_t *zilog, uint64_t syncval);

extern void	zil_set_logbias(zilog_t *zilog, uint64_t slogval);

extern uint64_t	zil_max_copied_data(zilog_t *zilog);
extern uint64_t	zil_max_log_data(zilog_t *zilog, size_t hdrsize);
extern itx_wr_state_t zil_write_state(zilog_t *zilog, uint64_t size,
    uint32_t blocksize, boolean_t o_direct, boolean_t commit);

extern void zil_sums_init(zil_sums_t *zs);
extern void zil_sums_fini(zil_sums_t *zs);
extern void zil_kstat_values_update(zil_kstat_values_t *zs,
    zil_sums_t *zil_sums);

extern int zil_replay_disable;
extern uint_t zfs_immediate_write_sz;
extern int zil_special_is_slog;

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_ZIL_H */
