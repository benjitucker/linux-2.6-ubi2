/*
 * Copyright (c) International Business Machines Corp., 2006
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
 * The UBI PEB Map (PMAP) sub-system.
 *
 * This sub-system is responsible for mapping logical blocks to physical
 * block chain.
 *
 * TODO - explanation text
 * TODO - move to an RB tree for faster lookups
 * TODO - Mutex protection of the PMAP?
 *
 */

#include <linux/slab.h>
#include <linux/crc32.h>
#include <linux/err.h>
#include "ubi.h"

/**
 * ubi_pmap_init - Initialise a PEB map
 * @ubi: UBI device description object
 *
 * This function initialises the PEB map data structure.
 * Returns pointer to allocated pmap structure on success or NULL if 
 * failed to allocate data structure.
 */

struct ubi_pmap *ubi_pmap_init(struct ubi_device *ubi)
{
	struct ubi_pmap *pmap = 
		vmalloc(ubi->peb_count * (sizeof struct ubi_pmap));
	if (pmap) {
		memset(pmap, 0, ubi->peb_count * (sizeof struct ubi_pmap));
	}
	return pmap;
}

/**
 * ubi_pmap_free - Initialise a PEB map
 * @ubi: UBI device description object
 * @peb_map: PEB Map data structure to be freed
 *
 * This function frees the PEB map data structure.
 */

void ubi_pmap_free(struct ubi_device *ubi, struct ubi_pmap *pmap)
{
	vfree(pmap);
}

/**
 * ubi_pmap_vol_reserved_area - Volume to PEB Range mapping
 * @vol_id: volume identifier
 * @first_peb: pointer to return the first PEB for the volume
 * @last_peb: pointer to return the last PEB for the volume
 *
 * Some volumes are grouped into particular blocks of PEBS.
 * Cueerntly this is only used for the layout volume which needs to occupy
 * to very first PEBs of the device
 */
static void ubi_pmap_vol_reserved_area(int vol_id, int *first_peb, int *last_peb)
{
	/* The layout volume is treated specially, as it has to reside in the
	 * first UBI_LAYOUT_VOLUME_RESERVED_EBS blocks of the device.
	 */
	if (vol_id == UBI_LAYOUT_VOLUME_ID) {
		*first_peb = 0;
		*last_peb = UBI_LAYOUT_VOLUME_RESERVED_EBS - 1;
	}
	else {
		*first_peb = UBI_LAYOUT_VOLUME_RESERVED_EBS;
		*last_peb = ubi->peb_count - 1;
	}
}

/**
 * ubi_pmap_lookup_pnum - Lookup a PEB number from the PEB map
 * @ubi: UBI device description object
 * @peb_map: PEB Map data structure
 * @vol: volume description object
 * @lnum: logical eraseblock number
 *
 * This function searches the PEB map data structure for the PEB number
 * mapped to the volume & LEB number within that volume.
 * Returns the PEB number if found or -1 otherwise.
 */

int ubi_pmap_lookup_pnum(struct ubi_device *ubi, struct ubi_pmap *peb_map,
			 int vol_id, int lnum)
{
	int pnum;
	struct ubi_pmap *pmap;
	int first_peb, last_peb;

	ubi_pmap_vol_reserved_area(vol_id, &first_peb, &last_peb);

	for (pnum = first_peb; pnum <= last_peb; ++pnum) {
		pmap = &peb_map[pnum];
		if (pmap->vol_id == vol_id &&
		    pmap->lnum == lnum && 
		    pmap->inuse &&
		    !pmap->bad) {
			
			return pnum;
		}
	}

	return -1;
}

#if 0 	// TODO - remove, now handled by resize
/**
 * ubi_pmap_add - Add a vol/lnum to pnum mapping to the PEB map
 * @ubi: UBI device description object
 * @peb_map: PEB Map data structure
 * @vol_id: volume identifier
 * @lnum: logical eraseblock number
 * @pnum: physical eraseblock number
 *
 * This function adds a mapping between vol/leb to head of list peb.
 * Returns zero on success, UBI_PMAP_INUSE if the peb is already inuse
 * and UBI_PMAP_BAD if the peb is marked bad.
 */

int ubi_pmap_add(struct ubi_device *ubi, struct ubi_pmap *peb_map,
		 int vol_id, int lnum, int pnum)
{
	struct ubi_pmap *pmap = &peb_map[pnum];

	if (pmap->inuse) {
		return UBI_PMAP_INUSE;
	}

	if (pmap->bad) {
		return UBI_PMAP_BAD;
	}

	pmap->inuse = 1;
	pmap->vol_id = vol_id;
	pmap->lnum = lnum;

	return 0;
}

/**
 * ubi_pmap_remove - Remove a vol/lnum to pnum mapping
 * @ubi: UBI device description object
 * @peb_map: PEB Map data structure
 * @vol_id: volume identifier
 * @lnum: logical eraseblock number
 * @pnum: physical eraseblock number
 *
 * This function removes a mapping between vol/leb to head of list peb.
 * Returns zero on success, UBI_PMAP_NOT_INUSE if the peb is not inuse
 * and UBI_PMAP_BAD if the peb is marked bad.
 */

int ubi_pmap_remove(struct ubi_device *ubi, struct ubi_pmap *peb_map,
		 int vol_id, int lnum, int pnum)
{
	struct ubi_pmap *pmap = &peb_map[pnum];

	if (!pmap->inuse) {
		return UBI_PMAP_NOT_INUSE;
	}

	if (pmap->bad) {
		return UBI_PMAP_BAD;
	}

	pmap->inuse = 0;

	return 0;
}
#endif

/**
 * ubi_pmap_allocate_volume - Allocate PEBs to an volume volume
 * @ubi: UBI device description object
 * @peb_map: PEB Map data structure
 * @vol_id: volume identifier
 * @peb: The first PEB number in a range to be allocated to the volume
 * @leb: The first LEB number in a range to be allocated to the volume
 * @blocks: The number of blocks to add
 * @bad: Bad PEB flag
 *
 * This function adds PEBs associated with a volume.
 * A range of blocks is added which have consectuve PEB and LEB numbers.
 */
int ubi_pmap_allocate_volume(struct ubi_device *ubi, struct ubi_pmap *peb_map,
		 	   int vol_id, int peb, int leb, int blocks, int bad)
{
	int i;
	struct ubi_pmap *pmap;
	int first_peb, last_peb;

	ubi_pmap_vol_reserved_area(vol_id, &first_peb, &last_peb);

	/* Check that the range is within the bounds of that allowed for the
	 * volume.
	 */
	if (peb < first_peb || (peb + blocks) > (last_peb + 1)) {
		ubi_err("blocks allocated outside area perscribed for "
			"vol_id %d", vol_id);
		return -EINVAL;
	}

	/* Check that the range is not already allocated */
	for (i = 0; i < blocks; ++i) {
		pmap = &peb_map[peb + i];
		if ((pmap->vol_id == vol_id) && 
			pmap->inuse) {

			ubi_err("block already allocated vol_id %d peb %d",
				 vol_id, peb + i);
			return -EINVAL;
		}
	}

	/* Allocate the PEBs to the volume */
	for (i = 0; i < blocks; ++i) {
		pmap = &peb_map[peb + i];
		pmap->vol_id = vol_id;
		pmap->lnum = leb + i;
		if (!bad) {
			pmap->inuse = 1;
			pmap->bad = 0;
		}
		else {
			pmap->inuse = 0;
			pmap->bad = 1;
		}
	}

	return 0;
}

/**
 * ubi_pmap_resize_volume - Increase or decrease the size of a volume
 * @ubi: UBI device description object
 * @peb_map: PEB Map data structure
 * @vol_id: volume identifier
 * @reserved_pebs: The new size of the volume
 *
 * This function adds or removed PEBs associated with a volume.
 * blocks are always added or removed from the end of the logical volume.
 * PEBs chosen are those that are available with the lowest index first.
 * Returns zero on success
 */
/* TODO - No, wrong. We need to cope with bad blocks appearing in volumes
 * 	  which would cause the lebs to be out of order wrt pebs.
 *  When reducing the volume size, surely we need to remove LEBS that are 
 * unmapped!
 */
int ubi_pmap_resize_volume(struct ubi_device *ubi, struct ubi_pmap *peb_map,
		 	   int vol_id, int reserved_pebs)
{
	int pnum, lnum = 0;
	struct ubi_pmap *pmap;
	int first_peb, last_peb;

	ubi_pmap_vol_reserved_area(vol_id, &first_peb, &last_peb);

	/* If the new size is zero, delete all mappings for the volume */
	if (reserved_pebs == 0) {
		for (pnum = first_peb; pnum <= last_peb; ++pnum) {
			pmap = &peb_map[pnum];
			if (pmap->vol_id == vol_id) {
				memset(pmap, 0, sizeof(struct ubi_pmap));
			}
		}

		return 0;
	}

	for (pnum = first_peb; pnum <= last_peb; ++pnum) {
		pmap = &peb_map[pnum];
		if (pmap->vol_id == vol_id &&
		    pmap->inuse &&
		    !pmap->bad) {

			/* Count the number of pebs the volume already has */
			lnum++;
		}
	}

	/* if the size has increased, add more */
	if (lnum < reserved_pebs) {
		for (pnum = first_peb; pnum <= last_peb; ++pnum) {
			pmap = &peb_map[pnum];
			if (!pmap->inuse && !pmap->bad) {

				pmap->inuse = 1;
				pmap->vol_id = vol_id;
				pmap->lnum = lnum;
			//	pmap->mapped = 0;	TODO - handling mapped LEBs

				/* Count the number of new pebs */
				lnum++;

				if (lnum == reserved_pebs) {
					break;
				}
			}
		}
	}

	/* if the size has decreased, remove unmapped blocks */
	/* TODO - volume reduction is tricky. We have to remove LEBs off of the end
	 * (highest index) first but what if they are currently mapped??
	 * Look at the old UBI, how do they handle this??
	 */
#if 0
	if (lnum > reserved_pebs) {
		for (pnum = first_peb; pnum <= last_peb; ++pnum) {
			pmap = &peb_map[pnum];
			if (pmap->vol_id == vol_id &&
				pmap->inuse &&
				!pmap->bad &&
				!pmap->mapped) {

				/* Count the number of removed pebs */
				lnum--;

				pmap->inuse = 0;

				if (lnum == reserved_pebs) {
					break;
				}
			}
		}
	}
#endif
	
	return 0;
}

/**
 * ubi_pmap_markbad_replace - Mark a PEB as bad and replace it with another.
 * @ubi: UBI device description object
 * @peb_map: PEB Map data structure
 * @pnum: physical eraseblock number
 *
 * This function marks a peb as bad. If the PEB is in use it is replaced by 
 * another unused one.
 * Returns the replacement peb number, pnum if not replaced or negitive error
 * code.
 */

int ubi_pmap_markbad_replace(struct ubi_device *ubi, struct ubi_pmap *peb_map,
		     int pnum)
{
	struct ubi_pmap *pmap = &peb_map[pnum];
	struct ubi_pmap *replacment_pmap;
	int replacment_pnum, lnum = 0;
	int vol_id;
	int need_replacment = 0;
	int first_peb, last_peb;

	if (pmap->inuse && !pmap->bad)
	{
		need_replacment = 1;
	}

	pmap->bad = 1;
	pmap->inuse = 0;
	
	ubi_pmap_vol_reserved_area(pmap->vol_id, &first_peb, &last_peb);

	if (need_replacment) 
	{
		/* Search for a replacment */
		for (replacment_pnum = first_peb; 
			replacment_pnum <= last_peb; ++replacment_pnum) {
			replacment_pmap = &peb_map[replacment_pnum];
			if (!replacment_pmap->inuse && !replacment_pmap->bad) {

				replacment_pmap->inuse = 1;
				replacment_pmap->vol_id = pmap->vol_id;
				replacment_pmap->lnum = pmap->lnum;
			//	pmap->mapped = 0;	TODO - handling mapped LEBs
				return replacment_pnum;
			}
		}
	}
	else
	{	
		/* TODO - Maybe it would be more sensible to return an error here */
		return pnum;
	}

	return -ENOMEM;
}

