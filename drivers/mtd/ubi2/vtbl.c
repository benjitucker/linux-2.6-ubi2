/*
 * Copyright (c) International Business Machines Corp., 2006
 * Copyright (c) Nokia Corporation, 2006, 2007
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See
 * the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 * Author: Artem Bityutskiy (Битюцкий Артём)
 */

/*
 * This file includes volume table manipulation code. The volume table is an
 * on-flash table containing volume meta-data like name, number of reserved
 * physical eraseblocks, type, etc. The volume table is stored in the so-called
 * "layout volume".
 *
 * The layout volume is an internal volume which is organized as follows. It
 * consists of two logical eraseblocks - LEB 0 and LEB 1. Each logical
 * eraseblock stores one volume table copy, i.e. LEB 0 and LEB 1 duplicate each
 * other. This redundancy guarantees robustness to unclean reboots. The volume
 * table is basically an array of volume table records. Each record contains
 * full information about the volume and protected by a CRC checksum.
 *
 * The volume table is changed, it is first changed in RAM. Then LEB 0 is
 * erased, and the updated volume table is written back to LEB 0. Then same for
 * LEB 1. This scheme guarantees recoverability from unclean reboots.
 *
 * In this UBI implementation the on-flash volume table does not contain any
 * information about how many data static volumes contain. This information may
 * be found from the scanning data.
 *
 * But it would still be beneficial to store this information in the volume
 * table. For example, suppose we have a static volume X, and all its physical
 * eraseblocks became bad for some reasons. Suppose we are attaching the
 * corresponding MTD device, the scanning has found no logical eraseblocks
 * corresponding to the volume X. According to the volume table volume X does
 * exist. So we don't know whether it is just empty or all its physical
 * eraseblocks went bad. So we cannot alarm the user about this corruption.
 *
 * The volume table also stores so-called "update marker", which is used for
 * volume updates. Before updating the volume, the update marker is set, and
 * after the update operation is finished, the update marker is cleared. So if
 * the update operation was interrupted (e.g. by an unclean reboot) - the
 * update marker is still there and we know that the volume's contents is
 * damaged.
 */

#include <linux/crc32.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <asm/div64.h>
#include "ubi.h"

#ifdef CONFIG_MTD_UBI_DEBUG_PARANOID
static void paranoid_vtbl_check(const struct ubi_device *ubi);
#else
#define paranoid_vtbl_check(ubi)
#endif

/* Empty volume table record */
static struct ubi_vtbl_record empty_vtbl_record;
static struct ubi_vtbl_record empty_pmap_record;

/**
 * ubi_change_vtbl_record - change volume table record.
 * @ubi: UBI device description object
 * @idx: table index to change
 * @vtbl_rec: new volume table record
 *
 * This function changes volume table record @idx. If @vtbl_rec is %NULL, empty
 * volume table record is written. The caller does not have to calculate CRC of
 * the record as it is done by this function. Returns zero in case of success
 * and a negative error code in case of failure.
 */
int ubi_change_vtbl_record(struct ubi_device *ubi, int idx,
			   struct ubi_vtbl_record *vtbl_rec)
{
	int i, err;
	uint32_t crc;
	struct ubi_volume *layout_vol;

	ubi_assert(idx >= 0 && idx < ubi->vtbl_slots);
	layout_vol = ubi->volumes[vol_id2idx(ubi, UBI_LAYOUT_VOLUME_ID)];

	if (!vtbl_rec)
		vtbl_rec = &empty_vtbl_record;
	else {
		crc = crc32(UBI_CRC32_INIT, vtbl_rec, UBI_VTBL_RECORD_SIZE_CRC);
		vtbl_rec->crc = cpu_to_be32(crc);
	}

	memcpy(&ubi->vtbl[idx], vtbl_rec, sizeof(struct ubi_vtbl_record));
	for (i = 0; i < UBI_LAYOUT_VOLUME_EBS; i++) {
		err = ubi_eba_unmap_leb(ubi, layout_vol, i);
		if (err)
			return err;

		err = ubi_eba_write_leb(ubi, layout_vol, i, ubi->vtbl, 0,
					ubi->vtbl_size, UBI_LONGTERM);
		if (err)
			return err;
	}

	paranoid_vtbl_check(ubi);
	return 0;
}

/**
 * ubi_vtbl_rename_volumes - rename UBI volumes in the volume table.
 * @ubi: UBI device description object
 * @rename_list: list of &struct ubi_rename_entry objects
 *
 * This function re-names multiple volumes specified in @req in the volume
 * table. Returns zero in case of success and a negative error code in case of
 * failure.
 */
int ubi_vtbl_rename_volumes(struct ubi_device *ubi,
			    struct list_head *rename_list)
{
	int i, err;
	struct ubi_rename_entry *re;
	struct ubi_volume *layout_vol;

	list_for_each_entry(re, rename_list, list) {
		uint32_t crc;
		struct ubi_volume *vol = re->desc->vol;
		struct ubi_vtbl_record *vtbl_rec = &ubi->vtbl[vol->vol_id];

		if (re->remove) {
			memcpy(vtbl_rec, &empty_vtbl_record,
			       sizeof(struct ubi_vtbl_record));
			continue;
		}

		vtbl_rec->name_len = cpu_to_be16(re->new_name_len);
		memcpy(vtbl_rec->name, re->new_name, re->new_name_len);
		memset(vtbl_rec->name + re->new_name_len, 0,
		       UBI_VOL_NAME_MAX + 1 - re->new_name_len);
		crc = crc32(UBI_CRC32_INIT, vtbl_rec,
			    UBI_VTBL_RECORD_SIZE_CRC);
		vtbl_rec->crc = cpu_to_be32(crc);
	}

	layout_vol = ubi->volumes[vol_id2idx(ubi, UBI_LAYOUT_VOLUME_ID)];
	for (i = 0; i < UBI_LAYOUT_VOLUME_EBS; i++) {
		err = ubi_eba_unmap_leb(ubi, layout_vol, i);
		if (err)
			return err;

		err = ubi_eba_write_leb(ubi, layout_vol, i, ubi->vtbl, 0,
					ubi->vtbl_size, UBI_LONGTERM);
		if (err)
			return err;
	}

	return 0;
}

/**
 * vtbl_check - check if volume table is not corrupted and sensible.
 * @ubi: UBI device description object
 * @vtbl: volume table
 * @pmap: PEB map
 *
 * This function returns zero if @vtbl and @pmap are all right, %1 if CRC is
 * incorrect, and %-EINVAL if either contains inconsistent data.
 */
static int vtbl_check(const struct ubi_device *ubi,
		      const struct ubi_vtbl_record *vtbl,
		      const struct ubi_pmap_record *pmap)
{
	int i, n, reserved_pebs, alignment, data_pad, vol_type, name_len;
	int peb, leb, vol_id, flags;
	int upd_marker, err;
	uint32_t crc;
	const char *name;

	for (i = 0; i < ubi->vtbl_slots; i++) {
		cond_resched();

		reserved_pebs = be32_to_cpu(vtbl[i].reserved_pebs);
		alignment = be32_to_cpu(vtbl[i].alignment);
		data_pad = be32_to_cpu(vtbl[i].data_pad);
		upd_marker = vtbl[i].upd_marker;
		vol_type = vtbl[i].vol_type;
		name_len = be16_to_cpu(vtbl[i].name_len);
		name = &vtbl[i].name[0];

		crc = crc32(UBI_CRC32_INIT, &vtbl[i], UBI_VTBL_RECORD_SIZE_CRC);
		if (be32_to_cpu(vtbl[i].crc) != crc) {
			ubi_err("bad CRC at record %u: %#08x, not %#08x",
				 i, crc, be32_to_cpu(vtbl[i].crc));
			ubi_dbg_dump_vtbl_record(&vtbl[i], i);
			return 1;
		}

		if (reserved_pebs == 0) {
			if (memcmp(&vtbl[i], &empty_vtbl_record,
						UBI_VTBL_RECORD_SIZE)) {
				err = 2;
				goto bad;
			}
			continue;
		}

		if (reserved_pebs < 0 || alignment < 0 || data_pad < 0 ||
		    name_len < 0) {
			err = 3;
			goto bad;
		}

		if (alignment > ubi->leb_size || alignment == 0) {
			err = 4;
			goto bad;
		}

		n = alignment & (ubi->min_io_size - 1);
		if (alignment != 1 && n) {
			err = 5;
			goto bad;
		}

		n = ubi->leb_size % alignment;
		if (data_pad != n) {
			dbg_err("bad data_pad, has to be %d", n);
			err = 6;
			goto bad;
		}

		if (vol_type != UBI_VID_DYNAMIC && vol_type != UBI_VID_STATIC) {
			err = 7;
			goto bad;
		}

		if (upd_marker != 0 && upd_marker != 1) {
			err = 8;
			goto bad;
		}

		if (reserved_pebs > ubi->good_peb_count) {
			dbg_err("too large reserved_pebs %d, good PEBs %d",
				reserved_pebs, ubi->good_peb_count);
			err = 9;
			goto bad;
		}

		if (name_len > UBI_VOL_NAME_MAX) {
			err = 10;
			goto bad;
		}

		if (name[0] == '\0') {
			err = 11;
			goto bad;
		}

		if (name_len != strnlen(name, name_len + 1)) {
			err = 12;
			goto bad;
		}
	}

	/* Checks that all names are unique */
	for (i = 0; i < ubi->vtbl_slots - 1; i++) {
		for (n = i + 1; n < ubi->vtbl_slots; n++) {
			int len1 = be16_to_cpu(vtbl[i].name_len);
			int len2 = be16_to_cpu(vtbl[n].name_len);

			if (len1 > 0 && len1 == len2 &&
			    !strncmp(vtbl[i].name, vtbl[n].name, len1)) {
				ubi_err("volumes %d and %d have the same name"
					" \"%s\"", i, n, vtbl[i].name);
				ubi_dbg_dump_vtbl_record(&vtbl[i], i);
				ubi_dbg_dump_vtbl_record(&vtbl[n], n);
				return -EINVAL;
			}
		}
	}

	/* Checks that the pmap records are sensible */
	for (i = 0; i < ubi->pmap_slots; i++) {

		peb = be32_to_cpu(pmap[i].peb);
		leb = be32_to_cpu(pmap[i].leb);
		vol_id = be32_to_cpu(pmap[i].vol_id);
		flags = vtbl[i].flags;

		crc = crc32(UBI_CRC32_INIT, &pmap[i], UBI_PMAP_RECORD_SIZE_CRC);
		if (be32_to_cpu(pmap[i].crc) != crc) {
			ubi_err("bad pmap CRC at record %u: %#08x, not %#08x",
				 i, crc, be32_to_cpu(pmap[i].crc));
			ubi_dbg_dump_pmap_record(&pmap[i], i);
			return 1;
		}

		/* records should be in order of PEB */
		if (i > 0) {
			if (pmap[i].peb <= pmap[i-1].peb) {
				err = 13;
				goto bad_pmap;
			}
		}

		if (peb < 0 || leb < 0) {
			err = 14;
			goto bad_pmap;
		}

		if (flags & ~(UBI_PEB_FREE | UBI_PEB_BAD)) {
			err = 15;
			goto bad_pmap;
		}

		if (vol_id2idx(ubi, vol_id) > 
			ubi->vtbl_slots + UBI_INT_VOL_COUNT) {
			err = 16;
			goto bad_pmap;
		}
	}
	

	return 0;

bad:
	ubi_err("volume table check failed: record %d, error %d", i, err);
	ubi_dbg_dump_vtbl_record(&vtbl[i], i);
	return -EINVAL;

bad_pmap:
	ubi_err("volume table check failed on pmap: record %d, error %d",
		 i, err);
	ubi_dbg_dump_pmap_record(&pmap[i], i);
	return -EINVAL;
}

/**
 * create_vtbl - create a copy of volume table.
 * @ubi: UBI device description object
 * @si: scanning information
 * @copy: number of the volume table copy
 * @vtbl: contents of the volume table
 * @pmap_tbl: contents of the pmap table
 *
 * This function returns zero in case of success and a negative error code in
 * case of failure.
 */
//static int create_vtbl(struct ubi_device *ubi, struct ubi_scan_info *si,
static int create_vtbl(struct ubi_device *ubi,
		       int copy, void *vtbl, void *pmap_tbl)
{
	int leb, err, tries = 0;
//	static struct ubi_vid_hdr *vid_hdr;
//	struct ubi_scan_volume *sv;
//	struct ubi_scan_leb *new_seb, *old_seb = NULL;
	struct ubi_volume *layout_vol;

	layout_vol = ubi->volumes[vol_id2idx(ubi, UBI_LAYOUT_VOLUME_ID)];
	ubi_assert(layout_vol != NULL);

	ubi_msg("create volume table (copy #%d)", copy + 1);

#if 0
	vid_hdr = ubi_zalloc_vid_hdr(ubi, GFP_KERNEL);
	if (!vid_hdr)
		return -ENOMEM;
#endif

#if 0
	/*
	 * Check if there is a logical eraseblock which would have to contain
	 * this volume table copy was found during scanning. It has to be wiped
	 * out.
	 */
	sv = ubi_scan_find_sv(si, UBI_LAYOUT_VOLUME_ID);
	if (sv)
		old_seb = ubi_scan_find_seb(sv, copy);
#endif

	leb = copy * UBI_LAYOUT_VOLUME_EBS_PER_COPY;
	
retry:
#if 0
	new_seb = ubi_scan_get_free_peb(ubi, si);
	if (IS_ERR(new_seb)) {
		err = PTR_ERR(new_seb);
		goto out_free;
	}
#endif

#if 0	/* We do not have vid headers */
	vid_hdr->vol_type = UBI_VID_DYNAMIC;
	vid_hdr->vol_id = cpu_to_be32(UBI_LAYOUT_VOLUME_ID);
	vid_hdr->compat = UBI_LAYOUT_VOLUME_COMPAT;
	vid_hdr->data_size = vid_hdr->used_ebs =
			     vid_hdr->data_pad = cpu_to_be32(0);
	vid_hdr->lnum = cpu_to_be32(copy);
	vid_hdr->sqnum = cpu_to_be64(++si->max_sqnum);

	/* The EC header is already there, write the VID header */
	err = ubi_io_write_vid_hdr(ubi, new_seb->pnum, vid_hdr);
	if (err)
		goto write_error;
#endif

	/* Write the layout volume contents */
	//err = ubi_io_write_data(ubi, vtbl, new_seb->pnum, 0, ubi->vtbl_size);

	//TODO - use the write function of our new logical list API

	//TODO - what if these writes find bad blocks? the code will try to
	//update the volume table which will be locked fro writing already	

	/* Write the vtbl contents */
	err = ubi_eba_unmap_leb(ubi, layout_vol, leb);
	if (err)
		goto write_error;

	err = ubi_eba_write_leb(
		ubi, layout_vol, leb, vtbl, 0, ubi->vtbl_size, UBI_LONGTERM);
	if (err)
		goto write_error;

	/* Write the pmap contents */
	err = ubi_eba_unmap_leb(ubi, layout_vol, leb+1);
	if (err)
		goto write_error;

	err = ubi_eba_write_leb(
		ubi, layout_vol, leb+1, pmap_tbl, 0, ubi->pmap_size, UBI_LONGTERM);
	if (err)
		goto write_error;

	// TODO - along with scanning for the volume table, we need to write it to
	// several locations incase the write failed.

#if 0
	/*
	 * And add it to the scanning information. Don't delete the old
	 * @old_seb as it will be deleted and freed in 'ubi_scan_add_used()'.
	 */
	err = ubi_scan_add_used(ubi, si, new_seb->pnum, new_seb->ec,
				vid_hdr, 0);
	kfree(new_seb);
	ubi_free_vid_hdr(ubi, vid_hdr);
#endif
	return err;

write_error:
#if 0	// TODO - need some design around this area
	if (err == -EIO && ++tries <= 5) {
		/*
		 * Probably this physical eraseblock went bad, try to pick
		 * another one.
		 */
		
		goto retry;
	}
#endif
out_free:
//	ubi_free_vid_hdr(ubi, vid_hdr);
	return err;

}

/**
 * process_lvol - process the layout volume.
 * @ubi: UBI device description object
 *
 * This function is responsible for reading the layout volume, ensuring it is
 * not corrupted, and recovering from corruptions if needed. Returns zero
 * in case of success and a negative error code in case of failure.
 */
static int process_lvol(struct ubi_device *ubi)
{
	int err;
	int i;
	struct ubi_vtbl_record *leb[UBI_LAYOUT_VOLUME_COPIES] = { NULL, NULL };
	struct ubi_pmap_record *leb_pmap[UBI_LAYOUT_VOLUME_COPIES] = { NULL, NULL };
	int copy_corrupted[UBI_LAYOUT_VOLUME_COPIES] = {1, 1};
	struct ubi_volume *layout_vol;

	layout_vol = ubi->volumes[vol_id2idx(ubi, UBI_LAYOUT_VOLUME_ID)];
	ubi_assert(layout_vol != NULL);

	/*
	 * UBI goes through the following steps when it changes the two copies 
	 * of the layout volume:
	 * a. erase copy 0;
	 * b. write new data to copy 0;
	 * c. erase copy 1;
	 * d. write new data to copy 1.
	 *
	 * Before the change, both copies contain the same data.
	 *
	 * Due to unclean reboots, the contents of copy 0 may be lost, but there
	 * should copy 1. So it is OK if copy 0 is corrupted while copy 1 is not.
	 * Similarly, copy 1 may be lost, but there should be copy 0. And
	 * finally, unclean reboots may result in a situation when neither copy 
	 * 0 nor copy 1 are corrupted, but they are different. In this case, copy
	 * 0 contains more recent information.
	 *
	 * So the plan is to first check copy 0. Then
	 * a. if copy 0 is OK, it must be containing the most recent data; then
	 *    we compare it with copy 1, and if they are different, we copy 
	 *    0 to 1;
	 * b. if copy 0 is corrupted, but copy 1 has to be OK, and we copy 1
	 *    to 0.
	 */

	dbg_gen("check layout volume");

	/* Read both copies of the vtbl and pmap into memory */
	for (i = 0; i < UBI_LAYOUT_VOLUME_COPIES; 
		i += UBI_LAYOUT_VOLUME_EBS_PER_COPY) {

		leb[i] = vmalloc(ubi->vtbl_size);
		if (!leb[i]) {
			err = -ENOMEM;
			goto out_free;
		}
		memset(leb[i], 0, ubi->vtbl_size);

		leb_pmap[i] = vmalloc(ubi->pmap_size);
		if (!leb_pmap[i]) {
			err = -ENOMEM;
			goto out_free;
		}
		memset(leb_pmap[i], 0, ubi->pmap_size);

		/* read the vtbl leb */
		err = ubi_eba_read_leb(
			ubi, layout_vol, i, leb[i], 0, ubi->vtbl_size, 0);
		if (err) {
			/* If the read failed, fall back to the other copy */
			err = 0;
		}

		/* read the pmap leb that follows the vtbl */
		err = ubi_eba_read_leb(
			ubi, layout_vol, i+1, pmap_leb[i], 0, ubi->pmap_size, 0);
		if (err) {
			/* If the read failed, fall back to the other copy */
			err = 0;
		}
	}

#if 0 // TODO - remove old implementation
	/* Read both LEB 0 and LEB 1 into memory */
	ubi_rb_for_each_entry(rb, seb, &sv->root, u.rb) {
		leb[seb->lnum] = vmalloc(ubi->vtbl_size);
		if (!leb[seb->lnum]) {
			err = -ENOMEM;
			goto out_free;
		}
		memset(leb[seb->lnum], 0, ubi->vtbl_size);

		err = ubi_io_read_data(ubi, leb[seb->lnum], seb->pnum, 0,
				       ubi->vtbl_size);
		if (err == UBI_IO_BITFLIPS || err == -EBADMSG)
			/*
			 * Scrub the PEB later. Note, -EBADMSG indicates an
			 * uncorrectable ECC error, but we have our own CRC and
			 * the data will be checked later. If the data is OK,
			 * the PEB will be scrubbed (because we set
			 * seb->scrub). If the data is not OK, the contents of
			 * the PEB will be recovered from the second copy, and
			 * seb->scrub will be cleared in
			 * 'ubi_scan_add_used()'.
			 */
			seb->scrub = 1;
		else if (err)
			goto out_free;
	}
#endif

	err = -EINVAL;
	if (leb[0] && pmap_leb[0]) {
		copy_corrupted[0] = vtbl_check(ubi, leb[0], pmap_leb[0]);
		if (copy_corrupted[0] < 0)
			goto out_free;
	}

	if (!copy_corrupted[0]) {
		/* LEB 0 is OK */
		if (leb[1] && pmap_leb[1]) {
			if (!memcmp(leb[0], leb[1], ubi->vtbl_size) &&
			    !memcmp(pmap_leb[0], pmap_leb[1], ubi->pmap_size)) {
			copy_corrupted[1] = 0;
			}
		}

		if (copy_corrupted[1]) {
			ubi_warn("volume table copy #2 is corrupted");
			err = create_vtbl(ubi, 1, leb[0], pmap_leb[0]);
			if (err)
				goto out_free;
			ubi_msg("volume table was restored");
		}

		/* Both LEB 1 and LEB 2 are OK and consistent */
		vfree(leb[1]);
		vfree(pmap_leb[1]);
		ubi->vtbl = leb[0];
		ubi->ptbl = pmap_leb[0];
		return 0;
	} else {
		/* LEB 0 is corrupted or does not exist */
		if (leb[1]) {
			copy_corrupted[1] = 
				vtbl_check(ubi, leb[1], pmap_leb[1]);
			if (copy_corrupted[1] < 0)
				goto out_free;
		}
		if (copy_corrupted[1]) {
			/* Both LEB 0 and LEB 1 are corrupted */
			ubi_err("both volume tables are corrupted");
			goto out_free;
		}

		ubi_warn("volume table copy #1 is corrupted");
		err = create_vtbl(ubi, 0, leb[1], pmap_leb[1]);
		if (err)
			goto out_free;
		ubi_msg("volume table was restored");

		vfree(leb[0]);
		vfree(pmap_leb[0]);
		ubi->vtbl = leb[1];
		ubi->ptbl = pmap_leb[1];
		return 0;
	}

out_free:
	vfree(leb[0]);
	vfree(leb[1]);
	vfree(pmap_leb[0]);
	vfree(pmap_leb[1]);
	return err;
}

/**
 * create_empty_lvol - create empty layout volume.
 * @ubi: UBI device description object
 *
 * This function returns zero in case of success and a
 * negative error code in case of failure.
 */
static int create_empty_lvol(struct ubi_device *ubi)
{
	int i;
	struct ubi_vtbl_record *vtbl;
	struct ubi_pmap_record *pmap;

	vtbl = vmalloc(ubi->vtbl_size);
	if (!vtbl)
		return -ENOMEM;
	memset(vtbl, 0, ubi->vtbl_size);

	pmap = vmalloc(ubi->pmap_size);
	if (!pmap) {
		vfree(vtbl);
		return -ENOMEM;
	}
	memset(pmap, 0, ubi->pmap_size);

	for (i = 0; i < ubi->vtbl_slots; i++)
		memcpy(&vtbl[i], &empty_vtbl_record, UBI_VTBL_RECORD_SIZE);

	for (i = 0; i < ubi->pmap_slots; i++)
		memcpy(&ptbl[i], &empty_pmap_record, UBI_PMAP_RECORD_SIZE);

	/* reserve the first N PEBs for the layout volume */
	ptbl[0].peb = 0;
	ptbl[0].leb = 0;
	ptbl[0].vol_id = UBI_LAYOUT_VOLUME_ID;
	ptbl[0].flags = UBI_PMAP_INUSE;
	ptbl[0].crc = crc32(UBI_CRC32_INIT, &ptbl[0], UBI_PMAP_RECORD_SIZE_CRC);

	ptbl[1].peb = ptbl[0].peb + UBI_LAYOUT_VOLUME_SIZE;
	ptbl[1].crc = crc32(UBI_CRC32_INIT, &ptbl[1], UBI_PMAP_RECORD_SIZE_CRC);

	for (i = 0; i < UBI_LAYOUT_VOLUME_COPIES; i++) {
		int err;

		err = create_vtbl(ubi, i, vtbl, ptbl);
		if (err) {
			vfree(vtbl);
			vfree(pmap);
			return err;
		}
	}

	ubi->vtbl = vtbl;
	ubi->ptbl = ptbl;
	return 0;
}

/**
 * init_volumes - initialize volume information for existing volumes.
 * @ubi: UBI device description object
 * @vtbl: volume table
 *
 * This function allocates volume description objects for existing volumes 
 * except the layout volume which is passed in.
 * Returns zero in case of success and a negative error code in case of
 * failure.
 */
//static int init_volumes(struct ubi_device *ubi, const struct ubi_scan_info *si,
static int init_volumes(struct ubi_device *ubi,
			const struct ubi_vtbl_record *vtbl,
			const struct ubi_pmap_record *ptbl)
{
	int i, reserved_pebs = 0;
	struct ubi_volume *vol;

	/* Process the PEB Map table */
	for (i = 0; i < ubi->pmap_slots; i++) {
		
	}

	/* Process the volume table */

	/* Start with the layout volume as to resides at the first few PEBs */
	vol = ubi->volumes[vol_id2idx(ubi, UBI_LAYOUT_VOLUME_ID)];
	ubi_assert(vol != NULL);
	reserved_pebs += vol->reserved_pebs;

	for (i = 0; i < ubi->vtbl_slots; i++) {
		cond_resched();

		if (be32_to_cpu(vtbl[i].reserved_pebs) == 0)
			continue; /* Empty record */

		vol = kzalloc(sizeof(struct ubi_volume), GFP_KERNEL);
		if (!vol)
			return -ENOMEM;

		vol->reserved_pebs = be32_to_cpu(vtbl[i].reserved_pebs);
		vol->alignment = be32_to_cpu(vtbl[i].alignment);
		vol->data_pad = be32_to_cpu(vtbl[i].data_pad);
		vol->upd_marker = vtbl[i].upd_marker;
		vol->vol_type = vtbl[i].vol_type == UBI_VID_DYNAMIC ?
					UBI_DYNAMIC_VOLUME : UBI_STATIC_VOLUME;
		vol->name_len = be16_to_cpu(vtbl[i].name_len);
		vol->usable_leb_size = ubi->leb_size - vol->data_pad;
		memcpy(vol->name, vtbl[i].name, vol->name_len);
		vol->name[vol->name_len] = '\0';
		vol->vol_id = i;

		if (vtbl[i].flags & UBI_VTBL_AUTORESIZE_FLG) {
			/* Auto re-size flag may be set only for one volume */
			if (ubi->autoresize_vol_id != -1) {
				ubi_err("more than one auto-resize volume (%d "
					"and %d)", ubi->autoresize_vol_id, i);
				kfree(vol);
				return -EINVAL;
			}

			ubi->autoresize_vol_id = i;
		}

		ubi_assert(!ubi->volumes[i]);
		ubi->volumes[i] = vol;
		ubi->vol_count += 1;
		vol->ubi = ubi;
		reserved_pebs += vol->reserved_pebs;

		/*
		 * In case of dynamic volume UBI knows nothing about how many
		 * data is stored there. So assume the whole volume is used.
		 */
		if (vol->vol_type == UBI_DYNAMIC_VOLUME) {
			vol->used_ebs = vol->reserved_pebs;
			vol->last_eb_bytes = vol->usable_leb_size;
			vol->used_bytes =
				(long long)vol->used_ebs * vol->usable_leb_size;
			continue;
		}

#if 0	/* No scanning information */
		/* Static volumes only */
		sv = ubi_scan_find_sv(si, i);
		if (!sv) {
			/*
			 * No eraseblocks belonging to this volume found. We
			 * don't actually know whether this static volume is
			 * completely corrupted or just contains no data. And
			 * we cannot know this as long as data size is not
			 * stored on flash. So we just assume the volume is
			 * empty. FIXME: this should be handled.
			 */
			continue;
		}

		if (sv->leb_count != sv->used_ebs) {
			/*
			 * We found a static volume which misses several
			 * eraseblocks. Treat it as corrupted.
			 */
			ubi_warn("static volume %d misses %d LEBs - corrupted",
				 sv->vol_id, sv->used_ebs - sv->leb_count);
			vol->corrupted = 1;
			continue;
		}
#endif

		// TODO - We have no information on the used erase blocks
		//vol->used_ebs = sv->used_ebs;
		vol->used_ebs = 1;
		vol->used_bytes =
			(long long)(vol->used_ebs - 1) * vol->usable_leb_size;
		// TODO not sure about this
		//vol->used_bytes += sv->last_data_size;
		//vol->last_eb_bytes = sv->last_data_size;
	}

	/* And add the layout volume */
#if 0	// TODO - remove
	vol = kzalloc(sizeof(struct ubi_volume), GFP_KERNEL);
	if (!vol)
		return -ENOMEM;

	vol->reserved_pebs = UBI_LAYOUT_VOLUME_EBS;
	vol->alignment = 1;
	vol->vol_type = UBI_DYNAMIC_VOLUME;
	vol->name_len = sizeof(UBI_LAYOUT_VOLUME_NAME) - 1;
	memcpy(vol->name, UBI_LAYOUT_VOLUME_NAME, vol->name_len + 1);
	vol->usable_leb_size = ubi->leb_size;
	vol->used_ebs = vol->reserved_pebs;
	vol->last_eb_bytes = vol->reserved_pebs;
	vol->used_bytes =
		(long long)vol->used_ebs * (ubi->leb_size - vol->data_pad);
	vol->vol_id = UBI_LAYOUT_VOLUME_ID;
	vol->ref_count = 1;

	ubi_assert(!ubi->volumes[i]);
	ubi->volumes[vol_id2idx(ubi, vol->vol_id)] = vol;
	reserved_pebs += vol->reserved_pebs;
	ubi->vol_count += 1;
	vol->ubi = ubi;
#endif

	if (reserved_pebs > ubi->avail_pebs) {
		ubi_err("not enough PEBs, required %d, available %d",
			reserved_pebs, ubi->avail_pebs);
		if (ubi->corr_peb_count)
			ubi_err("%d PEBs are corrupted and not used",
				ubi->corr_peb_count);
	}
	ubi->rsvd_pebs += reserved_pebs;
	ubi->avail_pebs -= reserved_pebs;

	return 0;
}

/**
 * init_layout_volume - initialize volume information for layout volume.
 * @ubi: UBI device description object
 *
 * This function allocates volume description object for the layout volume.
 * Returns -ENOMEM if unable to allocate memory.
 * TODO documet other return codes
 */
static int init_layout_volume(struct ubi_device *ubi)
{
	int pnum, err, lnum;
	struct ubi_volume *lvol;
	struct ubi_pmap *pmap;

	/* And add the layout volume */
	lvol = kzalloc(sizeof(struct ubi_volume), GFP_KERNEL);
	if (!lvol)
		return -ENOMEM;

	/* Calculate the number of pebs from the block size and the size
	 * of the (volume table + pmap records) * 2
	 */
	lvol->reserved_pebs = UBI_LAYOUT_VOLUME_EBS;
	lvol->alignment = 1;
	lvol->vol_type = UBI_DYNAMIC_VOLUME;
	lvol->name_len = sizeof(UBI_LAYOUT_VOLUME_NAME) - 1;
	memcpy(lvol->name, UBI_LAYOUT_VOLUME_NAME, lvol->name_len + 1);
	lvol->usable_leb_size = ubi->leb_size;
	lvol->used_ebs = lvol->reserved_pebs;
	lvol->last_eb_bytes = lvol->reserved_pebs;
	lvol->used_bytes =
		(long long)lvol->used_ebs * (ubi->leb_size - lvol->data_pad);
	lvol->vol_id = UBI_LAYOUT_VOLUME_ID;
	lvol->ref_count = 1;
	ubi->volumes[vol_id2idx(ubi, lvol->vol_id)] = lvol;
	ubi->vol_count += 1;
	lvol->ubi = ubi;

	/* Setup the PEB map for the layout volume. Hardcode the first N
	 * good PEBS for the layout volume.
	 * We need to scan over bad PEBs to find the layout volume
	 * to avoid bad PEBs in the layout volume. We do not want to do
	 * a full scan, so this mini-scan looks for the first N good 
	 * PEBs which should contain the blocks of the layout volume, where
	 * N is the size (in blocks) of the layout volume.
	 * Note: We do not read or check the llp headers here as that is 
	 * handled by the volume processing function.
	 */
	lnum = 0;
	for (pnum = 0; pnum < ubi->peb_count; ++pnum) {
		pmap = &ubi->peb_map[pnum];

		/* Skip bad physical eraseblocks */
		err = ubi_io_is_bad(ubi, pnum);
		if (err < 0)
			return err;
		else if (err) {
			err = ubi_pmap_markbad(ubi, ubi->peb_map, pnum);
		}
		else {
			lnum++;
			if (lnum == lvol->reserved_pebs) {
				break;
			}
		}
	}

	/* Now that we have marked the first blocks as bad, we can grow the
	 * layout volume and it will occupy the correct blocks.
	 */
	err = ubi_pmap_resize_volume(
		ubi, ubi->peb_map, lvol->vol_id, lvol->reserved_pebs);

	return err;
}

/**
 * check_sv - check volume scanning information.
 * @vol: UBI volume description object
 * @sv: volume scanning information
 *
 * This function returns zero if the volume scanning information is consistent
 * to the data read from the volume tabla, and %-EINVAL if not.
 */
#if 0	// TODO - remove
static int check_sv(const struct ubi_volume *vol,
		    const struct ubi_scan_volume *sv)
{
	int err;

	if (sv->highest_lnum >= vol->reserved_pebs) {
		err = 1;
		goto bad;
	}
	if (sv->leb_count > vol->reserved_pebs) {
		err = 2;
		goto bad;
	}
	if (sv->vol_type != vol->vol_type) {
		err = 3;
		goto bad;
	}
	if (sv->used_ebs > vol->reserved_pebs) {
		err = 4;
		goto bad;
	}
	if (sv->data_pad != vol->data_pad) {
		err = 5;
		goto bad;
	}
	return 0;

bad:
	ubi_err("bad scanning information, error %d", err);
	ubi_dbg_dump_sv(sv);
	ubi_dbg_dump_vol_info(vol);
	return -EINVAL;
}
#endif

/**
 * check_scanning_info - check that scanning information.
 * @ubi: UBI device description object
 * @si: scanning information
 *
 * Even though we protect on-flash data by CRC checksums, we still don't trust
 * the media. This function ensures that scanning information is consistent to
 * the information read from the volume table. Returns zero if the scanning
 * information is OK and %-EINVAL if it is not.
 */
#if 0	// TODO - remove
static int check_scanning_info(const struct ubi_device *ubi,
			       struct ubi_scan_info *si)
{
	int err, i;
	struct ubi_scan_volume *sv;
	struct ubi_volume *vol;

	if (si->vols_found > UBI_INT_VOL_COUNT + ubi->vtbl_slots) {
		ubi_err("scanning found %d volumes, maximum is %d + %d",
			si->vols_found, UBI_INT_VOL_COUNT, ubi->vtbl_slots);
		return -EINVAL;
	}

	if (si->highest_vol_id >= ubi->vtbl_slots + UBI_INT_VOL_COUNT &&
	    si->highest_vol_id < UBI_INTERNAL_VOL_START) {
		ubi_err("too large volume ID %d found by scanning",
			si->highest_vol_id);
		return -EINVAL;
	}

	for (i = 0; i < ubi->vtbl_slots + UBI_INT_VOL_COUNT; i++) {
		cond_resched();

		sv = ubi_scan_find_sv(si, i);
		vol = ubi->volumes[i];
		if (!vol) {
			if (sv)
				ubi_scan_rm_volume(si, sv);
			continue;
		}

		if (vol->reserved_pebs == 0) {
			ubi_assert(i < ubi->vtbl_slots);

			if (!sv)
				continue;

			/*
			 * During scanning we found a volume which does not
			 * exist according to the information in the volume
			 * table. This must have happened due to an unclean
			 * reboot while the volume was being removed. Discard
			 * these eraseblocks.
			 */
			ubi_msg("finish volume %d removal", sv->vol_id);
			ubi_scan_rm_volume(si, sv);
		} else if (sv) {
			err = check_sv(vol, sv);
			if (err)
				return err;
		}
	}

	return 0;
}
#endif

/**
 * ubi_read_volume_table - read the volume table.
 * @ubi: UBI device description object
 *
 * This function reads volume and layout tables, checks them, recover from 
 * errors if needed or creates them if needed. Returns zero in case of success
 * and a negative error code in case of failure.
 */
//int ubi_read_volume_table(struct ubi_device *ubi, struct ubi_scan_info *si)
int ubi_read_volume_table(struct ubi_device *ubi)
{
	int i, err;
//	struct ubi_volume *lvol;
//	struct ubi_scan_volume *sv;

	empty_vtbl_record.crc = cpu_to_be32(0xf116c36b);
	empty_pmap_record.crc = cpu_to_be32(0xf116c36b);//TODO - work out the crc

	/*
	 * The number of supported volumes is limited by the eraseblock size
	 * and by the UBI_MAX_VOLUMES constant.
	 */
	ubi->vtbl_slots = ubi->leb_size / UBI_VTBL_RECORD_SIZE;
	if (ubi->vtbl_slots > UBI_MAX_VOLUMES)
		ubi->vtbl_slots = UBI_MAX_VOLUMES;

	ubi->vtbl_size = ubi->vtbl_slots * UBI_VTBL_RECORD_SIZE;
	ubi->vtbl_size = ALIGN(ubi->vtbl_size, ubi->min_io_size);

	/*
	 * The number of supported PEB Maps is limited by the eraseblock size
	 * and by the UBI_MAX_PMAP constant.
	 */
	ubi->pmap_slots = ubi->leb_size / UBI_PMAP_RECORD_SIZE;
	if (ubi->pmap_slots > UBI_MAX_PMAP)
		ubi->pmap_slots = UBI_MAX_PMAP;

	ubi->pmap_size = ubi->pmap_slots * UBI_PMAP_RECORD_SIZE;
	ubi->pmap_size = ALIGN(ubi->pmap_size, ubi->min_io_size);

#if 0
	// TODO - remove scanning information
	sv = ubi_scan_find_sv(si, UBI_LAYOUT_VOLUME_ID);
	if (!sv) {
		/*
		 * No logical eraseblocks belonging to the layout volume were
		 * found. This could mean that the flash is just empty. In
		 * this case we create empty layout volume.
		 *
		 * But if flash is not empty this must be a corruption or the
		 * MTD device just contains garbage.
		 */
		if (si->is_empty) {
			ubi->vtbl = create_empty_lvol(ubi, si);
			if (IS_ERR(ubi->vtbl))
				return PTR_ERR(ubi->vtbl);
		} else {
			ubi_err("the layout volume was not found");
			return -EINVAL;
		}
	} else {
		if (sv->leb_count > UBI_LAYOUT_VOLUME_EBS) {
			/* This must not happen with proper UBI images */
			dbg_err("too many LEBs (%d) in layout volume",
				sv->leb_count);
			return -EINVAL;
		}

		ubi->vtbl = process_lvol(ubi);
		if (IS_ERR(ubi->vtbl))
			return PTR_ERR(ubi->vtbl);
	}
#endif

	/*
	 * Initialise the in-RAM layout volume. We know where the layout volume
	 * resides on the device so we can create the object without reading 
	 * anything from flash. We will need this volume object so that we can 
	 * read the layout volume from flash.
	 */
	err = init_layout_volume(ubi);
	if (err)
		goto out_free;

	/* 
	 * Process the layout volume, which is located at the start of the
	 * Logical device. If the layout volume is found to be corrupted,
	 * write a new empty one.
	 */
	if (process_lvol(ubi)) {
		err = create_empty_lvol(ubi);
		if (err)
			goto out_free;
	}

	ubi->avail_pebs = ubi->good_peb_count - ubi->corr_peb_count;

	// TODO - Process the pmap volume and build the ubi->peb_map

	/*
	 * The layout volume is OK, initialize the corresponding in-RAM data
	 * structures.
	 */
	err = init_volumes(ubi, ubi->vtbl, ubi->pmap);
	if (err)
		goto out_free;

#if 0	// TODO - We need to do something equivelent, but I am not sure what,
	//		given that the LEBs have no info on their volume
	/*
	 * Make sure that the scanning information is consistent to the
	 * information stored in the volume table.
	 */
	err = check_scanning_info(ubi, si);
	if (err)
		goto out_free;
#endif

	return 0;

out_free:
	vfree(ubi->vtbl);
	for (i = 0; i < ubi->vtbl_slots + UBI_INT_VOL_COUNT; i++) {
		kfree(ubi->volumes[i]);
		ubi->volumes[i] = NULL;
	}
	return err;
}

#ifdef CONFIG_MTD_UBI_DEBUG_PARANOID

/**
 * paranoid_vtbl_check - check volume table.
 * @ubi: UBI device description object
 */
static void paranoid_vtbl_check(const struct ubi_device *ubi)
{
	if (vtbl_check(ubi, ubi->vtbl)) {
		ubi_err("paranoid check failed");
		BUG();
	}
}

#endif /* CONFIG_MTD_UBI_DEBUG_PARANOID */
