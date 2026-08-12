/*
 * Copyright (C) 2020-2026 ArtInChip Technology Co. Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 *  author: <qi.xu@artinchip.com>
 *  Desc:  refrence frame function
 *
 */

#include <stdlib.h>
#include <string.h>
#include "h264_decoder.h"

#define REFSWAP(type,a,b) do {type SWAP_tmp= b; b= a; a= SWAP_tmp;} while(0)

static void print_short_term(struct h264_dec_ctx *s)
{
	int i;
	struct h264_picture* pic = NULL;

	logi("====== print short-term ref ======");
	for(i=0; i<s->short_ref_count; i++) {
		pic = s->frame_info.short_ref[i];
		if(pic) {
			logd("i: %d, frame_num: %d, poc: %d, buf_idx: %d, structure: %d",
		  i, pic->frame_num, pic->poc, pic->buf_idx, pic->picture_structure);
		}

	}
}

static void print_long_term(struct h264_dec_ctx *s)
{
	int i;
	struct h264_picture* pic = NULL;

	logi("====== print long-term ref cnt: %d======", s->long_ref_count);
	for(i=0; i<s->long_ref_count; i++) {
		pic = s->frame_info.long_ref[i];
		if(pic) {
			logd("i: %d, frame_num: %d, poc: %d, buf_idx: %d, structure: %d",
		  i, pic->frame_num, pic->poc, pic->buf_idx, pic->picture_structure);
		}
	}
}

/**
 * Extract structure information about the picture described by pic_num in
 * the current decoding context (frame or field). Note that pic_num is
 * picture number without wrapping (so, 0<=pic_num<max_pic_num).
 * @param pic_num picture number for which to extract structure information
 * @param structure one of PICT_XXX describing structure of picture
 *                      with pic_num
 * @return frame number (short term) or long term index of picture
 *         described by pic_num
 */
static int pic_num_extract(struct h264_dec_ctx *s, int pic_num, int *structure)
{
	*structure = s->picture_structure;
	if (s->picture_structure != PICT_FRAME) {
		if (!(pic_num & 1))
			/* opposite field */
			*structure ^= PICT_FRAME;
		pic_num >>= 1;
	}

	return pic_num;
}

/**
 * Mark a picture as no longer needed for reference. The refmask
 * argument allows unreferencing of individual fields or the whole frame.
 * If the picture becomes entirely unreferenced, but is being held for
 * display purposes, it is marked as such.
 * @param refmask mask of fields to unreference; the mask is bitwise
 *                anded with the reference marking of pic
 * @return non-zero if pic becomes entirely unreferenced (except possibly
 *         for display purposes) zero if one of the fields remains in
 *         reference
 */
static int unreference_pic(struct h264_dec_ctx *s, struct h264_picture *pic, int refmask)
{
	int i = 0;
	logi("unreference_pic, buf_idx: %d, refrence: %d, refmask: %d, display: %d",
		pic->buf_idx, pic->refrence, refmask, pic->displayed_flag);
	if (pic->refrence &= refmask) {
		// if one field of this frame is unrefrence, do nothing
		return 0;
	} else {
		// if one field of this frame is unrefrence, set DELAYED_PIC_REF to this field
		for(i=0; s->frame_info.delayed_pic[i]; i++) {
			if(pic == s->frame_info.delayed_pic[i]) {
				pic->refrence = DELAYED_PIC_REF;
				break;
			}
		}

		// if this frame is unrefrence, return it
		if((pic->refrence == 0 || pic->refrence == 4) && pic->frame) {
			fm_decoder_put_frame(s->decoder.fm, pic->frame);
		}
		return 1;
	}
}

/**
 * Find a h264_picture in the short term reference list by frame number.
 * @param frame_num frame number to search for
 * @param idx the index into h->short_ref where returned picture is found
 *            undefined if no picture found.
 * @return pointer to the found picture, or NULL if no pic with the provided
 *                 frame number is found
 */
static struct h264_picture *find_short(struct h264_dec_ctx *h, int frame_num, int *idx)
{
	int i;

	for (i = 0; i < h->short_ref_count; i++) {
		struct h264_picture *pic = h->frame_info.short_ref[i];

		logd("%d %d %p\n", i, pic->frame_num, pic);
		if (pic->frame_num == frame_num) {
			*idx = i;
			return pic;
		}
	}
	return NULL;
}

/**
 * Remove a picture from the short term reference list by its index in
 * that list.  This does no checking on the provided index; it is assumed
 * to be valid. Other list entries are shifted down.
 * @param i index into h->short_ref of picture to remove.
 */
static void remove_short_at_index(struct h264_dec_ctx *s, int i)
{
	mpp_assert(i >= 0 && i < s->short_ref_count);
	s->frame_info.short_ref[i] = NULL;
	if (--s->short_ref_count)
		memmove(&s->frame_info.short_ref[i], &s->frame_info.short_ref[i + 1],
			(s->short_ref_count - i) * sizeof(struct h264_picture*));
}

/**
 * Remove a picture from the long term reference list by its index in
 * that list.
 * @return the removed picture or NULL if an error occurs
 */
static struct h264_picture *remove_long(struct h264_dec_ctx *s, int i, int ref_mask)
{
	struct h264_picture *pic;

	pic = s->frame_info.long_ref[i];
	if (pic) {
		if (unreference_pic(s, pic, ref_mask)) {
			mpp_assert(s->frame_info.long_ref[i]->long_ref == 1);
			s->frame_info.long_ref[i]->long_ref 	= 0;
			s->frame_info.long_ref[i]		= NULL;
			s->long_ref_count--;
		}
	}

	return pic;
}

static int split_field_copy(struct h264_ref *dest, struct h264_picture *src, int parity, int id_add)
{
	int match = !!(src->refrence & parity);

	if (match) {
		dest->refrence = src->refrence;
		dest->poc = src->poc;
		dest->pic_id = src->pic_id;
		dest->parent = src;
		if (parity != PICT_FRAME) {
			dest->refrence = parity;
			dest->pic_id *= 2;
			dest->pic_id += id_add;
			dest->poc = src->field_poc[parity == PICT_BOTTOM_FIELD];
			logd("dest: refrence: %d, poc: %d", dest->refrence, dest->poc);
		}
	}

	return match;
}

static int h264_build_def_list(struct h264_ref *def, struct h264_picture **in, int len, int is_long, int sel)
{
	int i[2] = {0};
	int index = 0;
	logd("h264_build_def_list  long-term: %d, structure: %d", is_long, sel);
	while(i[0]<len || i[1]<len) {
		while(i[0]<len && !(in[i[0]] && (in[i[0]]->refrence & sel)))
			i[0]++;
		while(i[1]<len && !(in[i[1]] && (in[i[1]]->refrence & (sel ^ 3))))
			i[1]++;

		if(i[0] < len) {
			in[i[0]]->pic_id = is_long? i[0] : in[i[0]]->frame_num;
			split_field_copy(&def[index++], in[i[0]++], sel, 1);
		}
		if (i[1] < len) {
			in[i[1]]->pic_id = is_long ? i[1] : in[i[1]]->frame_num;
			split_field_copy(&def[index++], in[i[1]++], sel ^ 3, 0);
		}
	}

	return index;
}

/*
order picture by poc

limit: poc of current picture
dir: 0-ascending order; 1-descending order
*/
static int add_sorted(struct h264_picture **sorted, struct h264_picture **src,
                      int len, int limit, int dir)
{
	int i, best_poc;
	int out_i = 0;

	for (;;) {
		best_poc = dir ? INT_MIN : INT_MAX;

		for (i = 0; i < len; i++) {
			const int poc = src[i]->poc;
			if (((poc > limit) ^ dir) && ((poc < best_poc) ^ dir)) {
				best_poc      = poc;
				sorted[out_i] = src[i];
			}
		}
		if (best_poc == (dir ? INT_MIN : INT_MAX))
			break;
		limit = sorted[out_i++]->poc - dir;
	}
	return out_i;
}

/*
 *
 */
static struct h264_picture* remove_short(struct h264_dec_ctx* s, int frame_num, int ref_mask)
{
	struct h264_picture* pic = NULL;
	int idx;

	pic = find_short(s, frame_num, &idx);
	if(pic) {
		if(unreference_pic(s, pic, ref_mask))
			remove_short_at_index(s, idx);
	}

	return pic;
}

static void generate_sliding_window_mmcos(struct h264_dec_ctx *s)
{
	struct mmco* mmco = s->sh.mmco;
	int nb_mmco = 0;
	struct h264_sps_info* cur_sps = s->sps_buffers[s->active_sps_id];

	if(s->short_ref_count &&
	   s->long_ref_count + s->short_ref_count >= cur_sps->max_num_ref_frames &&
	   !((s->picture_structure != PICT_FRAME) && !s->first_field && s->frame_info.cur_pic_ptr->refrence)) {
		   mmco[0].opcode 		= MMCO_SHORT2UNUSED;
		   mmco[0].short_pic_num 	= s->frame_info.short_ref[s->short_ref_count-1]->frame_num;
		   nb_mmco			= 1;

		   if(s->picture_structure != PICT_FRAME) {
			   mmco[0].short_pic_num *= 2;
			   mmco[1].opcode = MMCO_SHORT2UNUSED;
			   mmco[1].short_pic_num = mmco[0].short_pic_num + 1;
			   nb_mmco = 2;
		   }
	}

	s->sh.nb_mmco = nb_mmco;
}

/**
 * Handle MMCO_SHORT2LONG operation
 */
static void execute_mmco_short2long(struct h264_dec_ctx *s, struct h264_picture *pic,
                                   int long_arg, int short_idx)
{
	if (s->frame_info.long_ref[long_arg] != pic) {
		remove_long(s, long_arg, 0);
	}

	remove_short_at_index(s, short_idx);
	s->frame_info.long_ref[long_arg] = pic;
	if (s->frame_info.long_ref[long_arg]) {
		s->frame_info.long_ref[long_arg]->long_ref = 1;
		s->long_ref_count++;
	}
}

/**
 * Handle MMCO_LONG2UNUSED operation
 */
static void execute_mmco_long2unused(struct h264_dec_ctx *s, int long_arg)
{
	int structure, j;
	struct h264_picture *pic;

	j = pic_num_extract(s, long_arg, &structure);
	pic = s->frame_info.long_ref[j];
	if (!pic) {
		loge("mmco unref long failed");
		return;
	}

	remove_long(s, j, structure ^ PICT_FRAME);
}

/**
 * Handle MMCO_LONG operation - assign current picture as long term reference
 */
static int execute_mmco_long(struct h264_dec_ctx *s, int long_arg)
{
	int j;

	// Check if current picture is in short term list
	if (s->frame_info.short_ref[0] == s->frame_info.cur_pic_ptr) {
		loge("mmco: cannot assign current picture to short and long at the same time");
		remove_short_at_index(s, 0);
	}

	// Ensure current picture is not already assigned as a long ref at different index
	if (s->frame_info.cur_pic_ptr->long_ref) {
		for (j = 0; j < 32; j++) {
		if (s->frame_info.long_ref[j] == s->frame_info.cur_pic_ptr) {
			if (j != long_arg) {
			logw("mmco: cannot assign current picture to 2 long term references");
			}
			remove_long(s, j, 0);
		}
		}
	}

	if (s->frame_info.long_ref[long_arg] != s->frame_info.cur_pic_ptr) {
		remove_long(s, long_arg, 0);
		s->frame_info.long_ref[long_arg] = s->frame_info.cur_pic_ptr;
		s->frame_info.long_ref[long_arg]->long_ref = 1;
		s->long_ref_count++;
	}

	s->frame_info.cur_pic_ptr->refrence |= s->picture_structure;
	return 1; // Current reference assigned
}

/**
 * Handle MMCO_SET_MAX_LONG operation
 */
static void execute_mmco_set_max_long(struct h264_dec_ctx *s, int max_long_arg)
{
	int j;
	for (j = max_long_arg; j < 16; j++) {
		remove_long(s, j, 0);
	}
}

/**
 * Handle MMCO_RESET operation
 */
static void execute_mmco_reset(struct h264_dec_ctx *s)
{
	int j;

	while (s->short_ref_count) {
		remove_short(s, s->frame_info.short_ref[0]->frame_num, 0);
	}

	for (j = 0; j < 16; j++) {
		remove_long(s, j, 0);
	}

	s->sh.frame_num = 0;
	s->frame_info.cur_pic_ptr->frame_num = 0;
	s->frame_info.cur_pic_ptr->mmco_reset = 1;

	for (j = 0; j < MAX_DELAYED_PIC_COUNT; j++) {
		s->frame_info.last_pocs[j] = INT_MIN;
	}
}

/**
 * Execute MMCO operations for reference picture marking
 */
static int execute_mmco_operation(struct h264_dec_ctx *s, struct mmco *mmco, int i)
{
	int frame_num, structure, j;
	struct h264_picture *pic = NULL;

	logi("i: %d, mmco[i].opcode: %d, short_pic_num: %d, long_arg: %d",
		i, mmco[i].opcode, mmco[i].short_pic_num, mmco[i].long_arg);

	if (mmco[i].opcode == MMCO_SHORT2UNUSED || mmco[i].opcode == MMCO_SHORT2LONG) {
		frame_num = pic_num_extract(s, mmco[i].short_pic_num, &structure);
		pic = find_short(s, frame_num, &j);

		if (!pic) {
			if (mmco[i].opcode != MMCO_SHORT2LONG ||
				!s->frame_info.long_ref[mmco[i].long_arg] ||
				s->frame_info.long_ref[mmco[i].long_arg]->frame_num != frame_num) {
				loge("mmco: unref short failure");
			}
			return 0; // Continue to next MMCO
		}
	}

	switch (mmco[i].opcode) {
		case MMCO_SHORT2UNUSED:
		remove_short(s, frame_num, structure ^ PICT_FRAME);
		break;

		case MMCO_SHORT2LONG:
		execute_mmco_short2long(s, pic, mmco[i].long_arg, j);
		break;

		case MMCO_LONG2UNUSED:
		execute_mmco_long2unused(s, mmco[i].long_arg);
		break;

		case MMCO_LONG:
		return execute_mmco_long(s, mmco[i].long_arg);

		case MMCO_SET_MAX_LONG:
		execute_mmco_set_max_long(s, mmco[i].long_arg);
		break;

		case MMCO_RESET:
		execute_mmco_reset(s);
		break;

		default:
		loge("error mmco type(%d)", mmco[i].opcode);
	}

	return 0;
}

/**
 * Assign current picture reference when not explicitly assigned by MMCO
 */
static int assign_current_pic_reference(struct h264_dec_ctx *s)
{
	struct h264_picture *pic;

	logd("s->short_ref_count: %d", s->short_ref_count);

	if (s->short_ref_count && s->frame_info.short_ref[0] == s->frame_info.cur_pic_ptr) {
		s->frame_info.cur_pic_ptr->refrence |= s->picture_structure;
	} else if (s->frame_info.cur_pic_ptr->long_ref) {
		loge("illegal short term reference assignment for second field");
		return -1;
	} else {
		logd("s->frame_info.cur_pic_ptr->frame_num: %d", s->frame_info.cur_pic_ptr->frame_num);
		pic = remove_short(s, s->frame_info.cur_pic_ptr->frame_num, 0);
		if (pic) {
		loge("illegal short term buffer state detected");
		// return -1;
		}

		if (s->short_ref_count) {
		memmove(&s->frame_info.short_ref[1], &s->frame_info.short_ref[0],
			s->short_ref_count * sizeof(struct h264_picture*));
		}

		s->frame_info.short_ref[0] = s->frame_info.cur_pic_ptr;
		s->short_ref_count++;
		s->frame_info.cur_pic_ptr->refrence |= s->picture_structure;
	}

	return 0;
}

/**
 * Handle reference count overflow by discarding one frame
 */
static void handle_reference_overflow(struct h264_dec_ctx *s)
{
	int i;
	struct h264_picture *pic;
	struct h264_sps_info *cur_sps = s->sps_buffers[s->active_sps_id];

	if (s->long_ref_count + s->short_ref_count <= cur_sps->max_num_ref_frames) {
		return;
	}

	/* We have too many reference frames, probably due to corrupted
	* stream. Need to discard one frame. Prevents overrun of the
	* short_ref and long_ref buffers.
	*/
	if (s->long_ref_count && !s->short_ref_count) {
		for (i = 0; i < 16; i++) {
			if (s->frame_info.long_ref[i]) {
				break;
			}
		}
		remove_long(s, i, 0);
	} else {
		pic = s->frame_info.short_ref[s->short_ref_count - 1];
		remove_short(s, pic->frame_num, 0);
	}
}

void reference_refresh(struct h264_dec_ctx *s)
{
	int i = 0;
	for(i=0; i<s->long_ref_count; i++) {
		if(s->frame_info.long_ref[i]) {
			unreference_pic(s, s->frame_info.long_ref[i], 0);
			s->frame_info.long_ref[i] = NULL;
		}
	}
	s->long_ref_count = 0;

	for(i=0; i<s->short_ref_count; i++) {
		if(s->frame_info.short_ref[i]) {
			unreference_pic(s, s->frame_info.short_ref[i], 0);
			s->frame_info.short_ref[i] = NULL;
		}
	}
	s->short_ref_count = 0;
}

int init_ref_list(struct h264_dec_ctx *s)
{
	struct h264_slice_header* sh = &s->sh;
	int lens[2];
	int len;
	int i;

//	print_short_term(s);
//	print_long_term(s);

	if(sh->slice_type == H264_SLICE_B) {
		struct h264_picture* sorted[32];
		int cur_poc, list;

		if(s->picture_structure != PICT_FRAME) {
			cur_poc = s->frame_info.cur_pic_ptr->field_poc[s->picture_structure == PICT_BOTTOM_FIELD];
		} else {
			cur_poc = s->frame_info.cur_pic_ptr->poc;
		}

		for(list =0; list < 2; list++) {
			// sort short-term ref
			len  = add_sorted(sorted,     s->frame_info.short_ref, s->short_ref_count, cur_poc, 1^list);
			len += add_sorted(sorted+len, s->frame_info.short_ref, s->short_ref_count, cur_poc, 0^list);

			len  = h264_build_def_list(s->frame_info.def_ref_list[list], sorted,
				len, 0, s->picture_structure);
			len += h264_build_def_list(s->frame_info.def_ref_list[list]+len, s->frame_info.long_ref,
				16, 1, s->picture_structure);

			if(len < sh->num_ref_idx[list]) {
				memset(&s->frame_info.def_ref_list[list][len], 0,
					sizeof(struct h264_ref)* (sh->num_ref_idx[list]-len));
			}
			lens[list] = len;
		}

		// why ??? from ffmpeg
		if (lens[0] == lens[1] && lens[1] > 1) {
			for (i = 0; i < lens[0] &&
				s->frame_info.def_ref_list[0][i].parent->buf_idx ==
				s->frame_info.def_ref_list[1][i].parent->buf_idx; i++) {}

			if (i == lens[0]) {
				REFSWAP(struct h264_ref,s->frame_info.def_ref_list[1][0], s->frame_info.def_ref_list[1][1]);
			}
		}
	} else {
		len  = h264_build_def_list(s->frame_info.def_ref_list[0],     s->frame_info.short_ref,
			s->short_ref_count, 0, s->picture_structure);
		len += h264_build_def_list(s->frame_info.def_ref_list[0]+len, s->frame_info.long_ref,
			16, 1, s->picture_structure);

		if(len < sh->num_ref_idx[0]) {
			memset(&s->frame_info.def_ref_list[0][len], 0, sizeof(struct h264_ref)* (sh->num_ref_idx[0]-len));
		}
	}

/*
	int list = 0;
	struct h264_picture* pic = NULL;
	for(i=0; i<s->sh.num_ref_idx[0]; i++) {
		pic = s->frame_info.def_ref_list[0][i].parent;
		logd("list: %d, i: %d, buf_idx: %d, structure: %d, poc: %d", 0, i,
			pic->buf_idx, s->frame_info.def_ref_list[0][i].refrence,
			s->frame_info.def_ref_list[0][i].poc);
	}
*/
	return 0;
}

int ref_pic_list_reordering(struct h264_dec_ctx *s)
{
	struct h264_slice_header* sh = &s->sh;
	struct h264_picture* ref;
	int list, index, i;
	unsigned int abs_diff_pic_num;
	int pred;
	int frame_num;
	int pic_structure;

	logd("sh->list_count: %d", sh->list_count);
	for(list=0; list<sh->list_count; list++) {
		memcpy(s->frame_info.ref_list[list], s->frame_info.def_ref_list[list],
			sizeof(struct h264_ref)*sh->num_ref_idx[list]);

		int flag = read_bits(&s->gb, 1);
		if(!flag)
			continue;

		pred = sh->cur_pic_num;
		for(index=0; ; index++) {
			unsigned int modification_of_pic_nums_idc = read_ue_golomb(&s->gb);

			if(modification_of_pic_nums_idc > 3) {
				loge("not support modification_of_pic_nums_idc: %d", modification_of_pic_nums_idc);
				return -1;
			}

			if(modification_of_pic_nums_idc == 3)
				break;
			if(index >= sh->num_ref_idx[list]) {
				loge("index(%d) out of range(%d)", index, sh->num_ref_idx[list]);
				return -1;
			}

			switch(modification_of_pic_nums_idc) {
			case 0:
			case 1: {
				abs_diff_pic_num = read_ue_golomb(&s->gb) + 1;
				if(abs_diff_pic_num > sh->max_pic_num) {
					loge("abs_diff_pic_num(%d) > sh->max_pic_num(%d)", abs_diff_pic_num, sh->max_pic_num);
					return -1;
				}

				if(modification_of_pic_nums_idc == 0)
					pred -= abs_diff_pic_num;
				else
					pred += abs_diff_pic_num;
				pred &= sh->max_pic_num -1;

				frame_num = pic_num_extract(s, pred, &pic_structure);

				for(i=s->short_ref_count-1; i>=0; i--) {
					ref = s->frame_info.short_ref[i];
					if(ref->frame_num == frame_num && (ref->refrence & pic_structure))
						break;
				}
				if(i >= 0)
					ref->pic_id = pred;
				break;
			}
			case 2: {
				unsigned int pic_id = read_ue_golomb(&s->gb);
				int long_idx = pic_num_extract(s, pic_id, &pic_structure);
				if(long_idx > 31) {
					loge("long_idx(%d), error", long_idx);
					return -1;
				}
				ref = s->frame_info.long_ref[long_idx];

				if(ref && (ref->refrence & pic_structure)) {
					ref->pic_id = pic_id;
					i=0;
				} else {
					i = -1;
				}
				break;
			}
			default:
				loge("not support modification_of_pic_nums_idc: %d", modification_of_pic_nums_idc);
				return -1;
			}

			if(i < 0) {
				loge("i(%d) < 0", i);
				return -1;
			}

			for(i=index; i<sh->num_ref_idx[list]-1; i++) {
				if(s->frame_info.ref_list[list][i].parent &&
					ref->long_ref == s->frame_info.ref_list[list][i].parent->long_ref &&
					ref->pic_id == s->frame_info.ref_list[list][i].pic_id)
					break;
			}

			for(; i>index; i--) {
				s->frame_info.ref_list[list][i] = s->frame_info.ref_list[list][i-1];
			}
			s->frame_info.ref_list[list][index].refrence = ref->refrence;
			s->frame_info.ref_list[list][index].pic_id = ref->pic_id;
			s->frame_info.ref_list[list][index].poc = ref->poc;
			s->frame_info.ref_list[list][index].parent = ref;

			if(s->picture_structure != PICT_FRAME) {
				s->frame_info.ref_list[list][index].refrence = pic_structure;
				s->frame_info.ref_list[list][index].poc = ref->field_poc[pic_structure == PICT_BOTTOM_FIELD];
			}
		}
	}

	if(sh->slice_type == H264_SLICE_I) {
		s->frame_info.cur_pic_ptr->ref_count[0] = 0;
	} else if(sh->slice_type != H264_SLICE_B) {
		s->frame_info.cur_pic_ptr->ref_count[1] = 0;
	}
	for(list = 0; list < 2; list++) {
		s->frame_info.cur_pic_ptr->ref_count[list] = sh->num_ref_idx[list];
	}

	return 0;
}

static struct h264_picture* get_one_frame_from_delayed(struct h264_dec_ctx *s)
{
	int i;
	int min_poc = INT_MAX;
	int idx = 0;
	struct h264_picture* out_pic = NULL;

	if(s->frame_info.delay_pic_num == 0) {
		return NULL;
	}

	for(i=0; i<s->frame_info.delay_pic_num; i++) {
		if(s->frame_info.delayed_pic[i]->poc < min_poc) {
			min_poc = s->frame_info.delayed_pic[i]->poc;
			out_pic = s->frame_info.delayed_pic[i];
			idx = i;
		}
	}

	for(i=idx; i<s->frame_info.delay_pic_num; i++) {
		s->frame_info.delayed_pic[i] = s->frame_info.delayed_pic[i+1];
	}

	s->min_display_poc = out_pic->poc;
	s->frame_info.delay_pic_num--;

	return out_pic;
}

int render_all_delayed_frame(struct h264_dec_ctx *s)
{
	struct h264_picture* out_pic = NULL;

	logd("delay_pic_num: %d", s->frame_info.delay_pic_num);
	while(s->frame_info.delay_pic_num) {
		out_pic = get_one_frame_from_delayed(s);
		if(s->frame_info.delay_pic_num == 0 && s->eos) {
			out_pic->frame->mpp_frame.flags |= FRAME_FLAG_EOS;
		}

		logi("render frame poc: %d, buf_idx: %d, id: %d, refrence: %d",
			out_pic->poc, out_pic->buf_idx, out_pic->frame->mpp_frame.id, out_pic->refrence);

		// send this frame to render, not call decoder_put_frame here.
		// decoder_put_frame will be called in reference_refresh
		if (s->decoder.rotmir_flag) {
			fm_decoder_frame_to_render(s->decoder.fm, s->frame_info.picture[out_pic->rotmir_idx].frame, 1);
			fm_decoder_put_frame(s->decoder.fm, s->frame_info.picture[out_pic->rotmir_idx].frame);

			fm_decoder_frame_to_render(s->decoder.fm, out_pic->frame, 0);
		} else {
			fm_decoder_frame_to_render(s->decoder.fm, out_pic->frame, 1);
		}
		// if the out_pic is not used for refrence, return it
		if((!out_pic->nal_ref_idc[0] && out_pic->picture_structure == PICT_FRAME) ||
		  (!out_pic->nal_ref_idc[0] && !out_pic->nal_ref_idc[1] && out_pic->picture_structure != PICT_FRAME))
			fm_decoder_put_frame(s->decoder.fm, out_pic->frame);
	}
	return 0;
}

int execute_ref_pic_marking(struct h264_dec_ctx *s)
{
	int i;
	int current_ref_assigned = 0;
	struct mmco *mmco = s->sh.mmco;
	int mmco_cnt;

	print_short_term(s);
	print_long_term(s);

	if (!s->sh.explicit_ref_marking) {
		generate_sliding_window_mmcos(s);
	}
	mmco_cnt = s->sh.nb_mmco;
	logi("explicit_ref_marking: %d, mmco_cnt: %d", s->sh.explicit_ref_marking, mmco_cnt);

	// Execute all MMCO operations
	for (i = 0; i < mmco_cnt; i++) {
		int result = execute_mmco_operation(s, mmco, i);
		if (result > 0) {
			current_ref_assigned = result;
		}
	}

	logi("current_ref_assigned: %d", current_ref_assigned);

	// Assign reference to current picture if not done by MMCO
	if (!current_ref_assigned) {
		if (assign_current_pic_reference(s) != 0) {
			return -1;
		}
	}

	// Handle reference count overflow
	handle_reference_overflow(s);

	print_short_term(s);
	print_long_term(s);
	return 0;
}


/**
 * Update POC ordering history
 */
static void update_poc_ordering(struct h264_dec_ctx *s, struct h264_picture *cur_pic)
{
	int i;

	for (i = 0; i <= MAX_DELAYED_PIC_COUNT; i++) {
		if (i == MAX_DELAYED_PIC_COUNT || cur_pic->poc < s->frame_info.last_pocs[i]) {
			if (i) {
				s->frame_info.last_pocs[i - 1] = cur_pic->poc;
			}
			break;
		} else if (i) {
			s->frame_info.last_pocs[i - 1] = s->frame_info.last_pocs[i];
		}
	}
}

/**
 * Calculate out-of-order count based on POC ordering
 */
static int calculate_out_of_order_count(struct h264_dec_ctx *s, struct h264_picture *cur_pic)
{
	int i, out_of_order = 0;

	for (i = 0; i <= MAX_DELAYED_PIC_COUNT; i++) {
		if (i == MAX_DELAYED_PIC_COUNT || cur_pic->poc < s->frame_info.last_pocs[i]) {
			out_of_order = MAX_DELAYED_PIC_COUNT - i;
			break;
		}
	}

	if (s->sh.slice_type == H264_SLICE_B ||
		(s->frame_info.last_pocs[MAX_DELAYED_PIC_COUNT - 2] > INT_MIN &&
		s->frame_info.last_pocs[MAX_DELAYED_PIC_COUNT - 1] -
		(int64_t)s->frame_info.last_pocs[MAX_DELAYED_PIC_COUNT - 2] > 2)) {
		out_of_order = out_of_order > 1 ? out_of_order : 1;
	}

	return out_of_order;
}

/**
 * Update B-frame count based on out-of-order information
 */
static void update_b_frame_count(struct h264_dec_ctx *s, int out_of_order,
                                struct h264_sps_info *cur_sps)
{
	if (out_of_order == MAX_DELAYED_PIC_COUNT) {
		int i;
		loge("invalid poc %d < %d", s->frame_info.cur_pic_ptr->poc,
		s->frame_info.last_pocs[0]);
		for (i = 0; i < MAX_DELAYED_PIC_COUNT; i++) {
			s->frame_info.last_pocs[i] = INT_MIN;
		}
		s->frame_info.last_pocs[0] = s->frame_info.cur_pic_ptr->poc;
		s->frame_info.cur_pic_ptr->mmco_reset = 1;
	} else if (s->has_b_frames < out_of_order) {
		s->has_b_frames = out_of_order;
	}

	if (cur_sps->max_num_ref_frames > 13) {
		logi("force set has_b_frames to 0");
		s->has_b_frames = 0;
	}

	if (s->has_b_frames > s->b_frames_max_num) {
		s->has_b_frames = s->b_frames_max_num;
	}

	if (s->no_b_frame || s->drop_b_frame_en) {
		s->has_b_frames = 0;
	}
}

/**
 * Find picture with minimum POC in delayed list
 */
static struct h264_picture *find_min_poc_picture(struct h264_dec_ctx *s, int *min_idx)
{
	int i;
	int min_poc = INT_MAX;
	struct h264_picture *out_pic = NULL;

	for (i = 0; i < s->frame_info.delay_pic_num; i++) {
		if (s->frame_info.delayed_pic[i]->poc < min_poc) {
		min_poc = s->frame_info.delayed_pic[i]->poc;
		out_pic = s->frame_info.delayed_pic[i];
		*min_idx = i;
		}
	}

	return out_pic;
}

/**
 * Find picture with maximum POC in delayed list
 */
static struct h264_picture *find_max_poc_picture(struct h264_dec_ctx *s)
{
	int i;
	int max_poc = -1;
	struct h264_picture *max_poc_pic = NULL;

	for (i = 0; i < s->frame_info.delay_pic_num; i++) {
		if (s->frame_info.delayed_pic[i]->poc > max_poc) {
		max_poc = s->frame_info.delayed_pic[i]->poc;
		max_poc_pic = s->frame_info.delayed_pic[i];
		}
	}

	return max_poc_pic;
}

/**
 * Handle out-of-order frame by discarding it
 */
static void handle_out_of_order_frame(struct h264_dec_ctx *s, struct h264_picture *out_pic,
                                     int idx, int out_of_order)
{
	int i;

	if (!out_of_order) {
		return;
	}

	logw("out_of_order: %d, delay_num: %d, has_b_frame: %d",
		out_of_order, s->frame_info.delay_pic_num, s->has_b_frames);
	logw("discard this frame, poc: %d, buf_idx: %d", out_pic->poc, out_pic->buf_idx);

	out_pic->refrence &= ~DELAYED_PIC_REF;
	s->frame_info.delay_pic_num--;

	fm_decoder_frame_to_render(s->decoder.fm, out_pic->frame, 0);

	// If the out_pic is not used for reference, return it
	if ((!out_pic->nal_ref_idc[0] && out_pic->picture_structure == PICT_FRAME) ||
		(!out_pic->nal_ref_idc[0] && !out_pic->nal_ref_idc[1] &&
		out_pic->picture_structure != PICT_FRAME)) {
		fm_decoder_put_frame(s->decoder.fm, out_pic->frame);
	}

	for (i = idx; s->frame_info.delayed_pic[i]; i++) {
		s->frame_info.delayed_pic[i] = s->frame_info.delayed_pic[i + 1];
	}
}

/**
 * Output a frame from the delayed list
 */
static void output_delayed_frame(struct h264_dec_ctx *s, struct h264_picture *out_pic,
                                int idx, int out_of_order)
{
	int i;

	if (out_of_order || s->frame_info.delay_pic_num <= s->has_b_frames) {
		return;
	}

	if (idx == 0 && s->frame_info.delayed_pic[0] &&
		(s->frame_info.delayed_pic[0]->key_frame ||
		s->frame_info.delayed_pic[0]->mmco_reset)) {
		s->next_output_poc = INT_MIN;
	} else {
		s->next_output_poc = out_pic->poc;
	}

	out_pic->displayed_flag = 1;

	if (s->decoder.rotmir_flag) {
		fm_decoder_frame_to_render(s->decoder.fm, s->frame_info.picture[out_pic->rotmir_idx].frame, 1);
		fm_decoder_put_frame(s->decoder.fm, s->frame_info.picture[out_pic->rotmir_idx].frame);

		fm_decoder_frame_to_render(s->decoder.fm, out_pic->frame, 0);
	} else {
		fm_decoder_frame_to_render(s->decoder.fm, out_pic->frame, 1);
	}

	if (out_pic->refrence == 0 || out_pic->refrence == DELAYED_PIC_REF) {
		out_pic->refrence = 0;
		out_pic->displayed_flag = 0;
	}

	logi("render frame poc: %d, buf_idx: %d, frame_id: %d, refrence: %d",
		out_pic->poc, out_pic->buf_idx, out_pic->frame->mpp_frame.id, out_pic->refrence);

	logi("nal_ref_idc: %d %d", out_pic->nal_ref_idc[0], out_pic->nal_ref_idc[1]);

	// If the frame is not used for reference, it will not be used by decoder now
	if (!out_pic->nal_ref_idc[0] && !out_pic->nal_ref_idc[1]) {
		fm_decoder_put_frame(s->decoder.fm, out_pic->frame);
		out_pic->refrence = 0;
		out_pic->displayed_flag = 0;
	}

	for (i = idx; i < s->frame_info.delay_pic_num; i++) {
		s->frame_info.delayed_pic[i] = s->frame_info.delayed_pic[i + 1];
	}

	s->frame_info.delayed_pic[i] = NULL;
	s->frame_info.delay_pic_num--;
	s->min_display_poc = out_pic->poc;
	s->frame_info.delayed_output_pic = out_pic;
}

/**
 * Print delayed picture list for debugging
 */
static void print_delayed_pic_list(struct h264_dec_ctx *s)
{
	int i;

	logd("====== print delay pic =======");
	for (i = 0; i < s->frame_info.delay_pic_num; i++) {
		logd("delay pic: %d, %p", i, s->frame_info.delayed_pic[i]);
		if (s->frame_info.delayed_pic[i]) {
		logd("poc: %d, buf_idx: %d, flag: %x",
			s->frame_info.delayed_pic[i]->poc,
			s->frame_info.delayed_pic[i]->buf_idx,
			s->frame_info.delayed_pic[i]->frame->mpp_frame.flags);
		}
	}
}

void select_output_frame(struct h264_dec_ctx *s)
{
	int out_of_order, idx = 0;
	struct h264_sps_info *cur_sps = s->sps_buffers[s->active_sps_id];
	struct h264_picture *cur_pic = s->frame_info.cur_pic_ptr;
	struct h264_picture *out_pic = NULL;
	struct h264_picture *max_poc_pic = NULL;

	// 1. Update POC ordering and calculate out-of-order count
	update_poc_ordering(s, cur_pic);
	out_of_order = calculate_out_of_order_count(s, cur_pic);

	// 2. Update B-frame count based on out-of-order information
	update_b_frame_count(s, out_of_order, cur_sps);

	logd("s->has_b_frames: %d, b_frames_max_num: %d", s->has_b_frames, s->b_frames_max_num);

	// 3. Add current picture to delayed list
	s->frame_info.delayed_pic[s->frame_info.delay_pic_num++] = cur_pic;
	s->frame_info.delayed_pic[s->frame_info.delay_pic_num] = NULL;
	s->frame_info.cur_pic_ptr = NULL;

	logd("delay_pic_num: %d  %p", s->frame_info.delay_pic_num, s->frame_info.delayed_pic[0]);

	// 4. Find pictures with min and max POC
	out_pic = find_min_poc_picture(s, &idx);
	max_poc_pic = find_max_poc_picture(s);

	logd("min_poc: %d, s->curr_packet: %p", out_pic ? out_pic->poc : -1, s->curr_packet);
	logd("max_poc: %d, s->curr_packet flag: %d",
		max_poc_pic ? max_poc_pic->poc : -1, s->curr_packet->flag);

	// 5. Set EOS flag to the frame with max POC
	if (s->curr_packet->flag & PACKET_FLAG_EOS && max_poc_pic) {
		max_poc_pic->frame->mpp_frame.flags |= FRAME_FLAG_EOS;
	}

	if (s->has_b_frames == 0 && s->frame_info.delayed_pic[0] &&
		(s->frame_info.delayed_pic[0]->key_frame ||
		s->frame_info.delayed_pic[0]->mmco_reset)) {
		s->next_output_poc = INT_MIN;
	}

	out_of_order = out_pic && (out_pic->poc < s->next_output_poc);

	// If there is no b frame, the picture out of order is error
	if (s->no_b_frame) {
		if (out_of_order && out_pic) {
			out_pic->frame->mpp_frame.flags |= FRAME_FLAG_ERROR;
		}
		out_of_order = 0;
	}

	// Skip POC reordering when dropping B-frames
	if (s->drop_b_frame_en) {
		out_of_order = 0;
	}

	// 6. Handle out-of-order frame
	if (out_pic) {
		handle_out_of_order_frame(s, out_pic, idx, out_of_order);
	}

	logi("cur poc: %d, out poc: %d, next poc: %d",
		cur_pic->poc, out_pic->poc, s->next_output_poc);
	// 7. Output frame if conditions are met
	if (out_pic && !out_of_order) {
		output_delayed_frame(s, out_pic, idx, out_of_order);
	}

	// 8. Print delayed picture list for debugging
	print_delayed_pic_list(s);
}

void flush_all_delay_picture(struct h264_dec_ctx *s)
{
	int i = 0;
	struct h264_picture* pic = NULL;

	for(i=0; i<s->frame_info.delay_pic_num; i++) {
		pic = s->frame_info.delayed_pic[i];
		if(pic->frame) {
			fm_decoder_frame_to_render(s->decoder.fm, pic->frame, 0);
			fm_decoder_put_frame(s->decoder.fm, pic->frame);
		}
		pic->refrence = 0;
		pic->frame = NULL;
	}
	s->frame_info.delay_pic_num = 0;
}
