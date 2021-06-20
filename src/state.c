/*
 * Copyright (C) 2021  Brodie Gaslam
 *
 *  This file is part of "fansi - ANSI Control Sequence Aware String Functions"
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Go to <https://www.r-project.org/Licenses/GPL-2> for a copy of the license.
 */

#include "fansi.h"

/*
 * Create a state structure with everything set to zero
 *
 * We rely on struct initialization to set everything else to zero.
 *
 * FANSI_state_init_full is specifically to handle the allowNA case in nchar,
 * for which we MUST check `state.nchar_err` after each `FANSI_read_next`.  In
 * all other cases `R_nchar` shoudl be set to not `allowNA`.
 */
struct FANSI_state FANSI_state_init_full(
  SEXP strsxp, SEXP warn, SEXP term_cap, SEXP allowNA, SEXP keepNA,
  SEXP width, SEXP ctl, R_xlen_t i
) {
  // nocov start
  if(TYPEOF(strsxp) != STRSXP) {
    error(
      "Internal error: state_init with bad type for strsxp (%s)",
      type2char(TYPEOF(strsxp))
    );
  }
  if(i < 0 || i > XLENGTH(strsxp))
    error(
      "Internal error: state_init with out of bounds index [%jd] for strsxp.",
      FANSI_ind(i)
    );
  SEXP chrsxp = STRING_ELT(strsxp, i);
  FANSI_check_chrsxp(chrsxp, i);
  const char * string = CHAR(chrsxp);

  // Validation not complete here, many of these should be scalar, rely on R
  // level checks.
  if(TYPEOF(warn) != LGLSXP)
    error(
      "Internal error: state_init with bad type for warn (%s)",
      type2char(TYPEOF(warn))
    );
  if(TYPEOF(term_cap) != INTSXP)
    error(
      "Internal error: state_init with bad type for term_cap (%s)",
      type2char(TYPEOF(term_cap))
    );
  if(TYPEOF(allowNA) != LGLSXP)
    error(
      "Internal error: state_init with bad type for allowNA (%s)",
      type2char(TYPEOF(allowNA))
    );
  if(TYPEOF(keepNA) != LGLSXP)
    error(
      "Internal error: state_init with bad type for keepNA (%s)",
      type2char(TYPEOF(keepNA))
    );
  if(TYPEOF(width) != INTSXP)
    error(
      "Internal error: state_init with bad type for width (%s)",
      type2char(TYPEOF(width))
    );
  if(TYPEOF(ctl) != INTSXP)
    error(
      "Internal error: state_init with bad type for ctl (%s)",
      type2char(TYPEOF(ctl))
    );

  // nocov end

  int * term_int = INTEGER(term_cap);
  int warn_int = asInteger(warn);
  int term_cap_int = 0;

  R_xlen_t i_len = XLENGTH(term_cap);
  for(R_xlen_t i = 0; i < i_len; ++i) {
    // we limit term cap to 31 to avoid potential bit shift issues with a signed
    // int.  In practice we shouldn't get anywhere close to this anwyay.
    if(term_int[i] > 31 || term_int[i] < 1)
      // nocov start
      error("Internal Error: bit flag value for term_cap illegal.");
      // nocov end

    term_cap_int |= 1 << (term_int[i] - 1);
  }
  return (struct FANSI_state) {
    .sgr = (struct FANSI_sgr) {.color = -1, .bg_color = -1},
    .sgr_prev = (struct FANSI_sgr) {.color = -1, .bg_color = -1},
    .string = string,
    .warn = warn_int,
    .term_cap = term_cap_int,
    .allowNA = asLogical(allowNA),
    .keepNA = asLogical(keepNA),
    .use_nchar = asInteger(width),  // 0 for chars, 1 for width
    .ctl = FANSI_ctl_as_int(ctl)
  };
}
// When we don't care about R_nchar width, but do care about CSI / SGR (which
// means, we only really care about SGR since all CSI does is affect width calc).

struct FANSI_state FANSI_state_init(
  SEXP strsxp, SEXP warn, SEXP term_cap, R_xlen_t i
) {
  int prt = 0;
  SEXP R_false = PROTECT(ScalarLogical(0)); ++prt;
  SEXP R_true = PROTECT(ScalarLogical(1)); ++prt;
  SEXP R_zero = PROTECT(ScalarInteger(0)); ++prt;
  SEXP R_one = PROTECT(ScalarInteger(1)); ++prt;
  struct FANSI_state res = FANSI_state_init_full(
    strsxp, warn, term_cap,
    R_true,  // allowNA for invalid multibyte
    R_false, // keepNA
    R_zero,  // Don't use width by default
    R_one,   // Treat all escapes as special by default (wrong prior to v1.0)
    i
  );
  UNPROTECT(prt);
  return res;
}

struct FANSI_state FANSI_reset_width(struct FANSI_state state) {
  state.pos_width = 0;
  state.pos_width_target = 0;
  return state;
}
// Error message specific to use in adding spaces for tabs.
struct FANSI_state FANSI_inc_width(
  struct FANSI_state state, int inc, R_xlen_t i
) {
  if(state.pos_width_target > FANSI_lim.lim_int.max - inc)
    error(
      "Expanding tabs will cause string to exceed INT_MAX at index [%ju].",
      FANSI_ind(i)
    );

  state.pos_width_target += inc;
  state.pos_width += inc;
  return state;
}
/*
 * Reset the position counters
 *
 * Intended so that all the state info is kept for when state persists from one
 * element in a character vector to the next.
 *
 * We are not 100% sure we're resetting everything that needs to be reset.
 */
struct FANSI_state FANSI_reset_pos(struct FANSI_state state) {
  state.pos_byte = 0;
  state.pos_ansi = 0;
  state.pos_raw = 0;
  state.pos_width = 0;
  state.pos_width_target = 0;
  state.last_esc = 0;
  state.terminal = 0;
  state.non_normalized = 0;
  return state;
}
/*
 * Compute the state given a character position (raw position)
 *
 * This function is designed to be called iteratively on the same string with
 * monotonically increasing values of `pos`.  This allows us to compute state at
 * multiple positions while re-using the work we did on the earlier positions.
 *
 *
 * @param pos the minimum number of characters or width units, to include
 *   (excluding SGR and similar).
 * @param state the last state that was known not to exceed prior target.
 * @param int type whether to use character (0), width (1), or byte (2) when
 *   computing the position (looks like we don't use 2?)
 * @param keep whether a partially started width unit should be kept (only
 *   meaningful in type = 1 (width) mode
 */

static struct FANSI_state_pair state_at_pos2(
  int pos, struct FANSI_state state, int type, int keep, R_xlen_t i
) {
  if(type != 0 && type != 1)
    error("Internal Error: type must be 0 or 1.");  // nocov
  struct FANSI_state state_prev = state;
  int pos_new = type ? state.pos_width : state.pos_raw;

  if(pos < pos_new)
    // nocov start
    error(
      "Internal Error: %s (%d) than `pos` (%d)",
      "Cannot re-use a state for a later position",
      pos_new, pos
    );
    // nocov end

  while(pos_new <= pos && state.string[state.pos_byte]) {
    state_prev = state;
    state = FANSI_read_next(state, i);
    pos_new = type ? state.pos_raw : state.pos_width;
  }
  int pos_prev = type ? state_prev.pos_raw : state_prev.pos_width;

  struct FANSI_state state_res;
  if(pos_new > pos && pos_prev == pos) {
    // Overshot, but prior was just right.  We allow overshoot to make
    // sure we get all zero width elements read before stopping (even in
    // type 0 there will be some of these: e.g. SGR sequences).
    state_res = state_prev;
  } else if (pos_new > pos && pos_prev < pos && keep) {
    // Overshoot, but only partial char (width mode only)
    state_res = state;
  } else if (pos_new > pos && pos_prev) {
    // Same, but don't keep.
    state_res = state_prev;
  } else if (state.string[state.pos_byte]) {
    // Finished string without getting position
    state_res = state;
  } else
    error("Internal error, unexpected state at pos read result."); // nocov

  // Avoid potential double warning next time we read
  state_prev.warn = state_res.warn;

  return (struct FANSI_state_pair){.cur=state_res, .prev=state_prev};
}
/*
 * We always include the size of the delimiter; could be a problem that this
 * isn't the actual size, but rather the maximum size (i.e. we always assume
 * three bytes even if the numbers don't get into three digits).
 */
int FANSI_color_size(int color, int * color_extra) {
  int size = 0;
  if(color == 8 && color_extra[0] == 2) {
    size = 3 + 2 +
      FANSI_digits_in_int(color_extra[1]) + 1 +
      FANSI_digits_in_int(color_extra[2]) + 1 +
      FANSI_digits_in_int(color_extra[3]) + 1;
  } else if (color == 8 && color_extra[0] == 5) {
    size = 3 + 2 +
      FANSI_digits_in_int(color_extra[1]) + 1;
  } else if (color == 8) {
    error("Internal Error: unexpected compound color format");   // nocov
  } else if (color >= 0 && color < 10) {
    size = 3;
  } else if (color >= 90 && color <= 97) {
    size = 3;
  } else if (color >= 100 && color <= 107) {
    size = 4;
  } else if (color > 0) {
    error("Internal Error: unexpected color format"); // nocov
  }
  return size;
}
/*
 * Generate the ANSI tag corresponding to the state and write it out as a NULL
 * terminated string.
 */
char * FANSI_sgr_as_chr(
  struct FANSI_buff *buff, struct FANSI_sgr sgr, int normalize, R_xlen_t i
) {
  // First pass computes total size of tag
  char * buff_track = NULL;
  int tag_len = FANSI_W_sgr(&buff_track, sgr, 0, normalize, i);
  FANSI_size_buff(buff, tag_len);
  buff_track = buff->buff;
  FANSI_W_sgr(&buff_track, sgr, 0, normalize, i);
  return buff->buff;
}
/*
 * Determine whether two state structs have same style
 *
 * This only compares the style pieces (i.e. not the position pieces)
 *
 * Returns 1 if the are different, 0 if they are equal.
 *
 * _basic is used just for the 1-9 SGR codes plus colors.
 */
int FANSI_sgr_comp_color(
  struct FANSI_sgr target, struct FANSI_sgr current
) {
  return !(
    target.color == current.color &&
    target.bg_color == current.bg_color &&
    target.color_extra[0] == current.color_extra[0] &&
    target.bg_color_extra[0] == current.bg_color_extra[0] &&
    target.color_extra[1] == current.color_extra[1] &&
    target.bg_color_extra[1] == current.bg_color_extra[1] &&
    target.color_extra[2] == current.color_extra[2] &&
    target.bg_color_extra[2] == current.bg_color_extra[2] &&
    target.color_extra[3] == current.color_extra[3] &&
    target.bg_color_extra[3] == current.bg_color_extra[3]
  );
}
int FANSI_sgr_comp_basic(
  struct FANSI_sgr target, struct FANSI_sgr current
) {
  // 1023 is '11 1111 1111' in binary, so this will grab the last ten bits
  // of the styles which are the 1-9 styles
  return FANSI_sgr_comp_color(target, current) ||
    (target.style & 1023) != (current.style & 1023);
}
int FANSI_sgr_comp(struct FANSI_sgr target, struct FANSI_sgr current) {
  return !(
    !FANSI_sgr_comp_basic(target, current) &&
    target.style == current.style &&
    target.border == current.border &&
    target.font == current.font &&
    target.ideogram == current.ideogram
  );
}
/*
 * Create a new SGR that has all the styles in `old` missing from `new`.
 *
 * This is so that we can then generate the closing SGRs required to transition
 * from one state to the other (used for diff).
 */
struct FANSI_sgr FANSI_sgr_setdiff(struct FANSI_sgr old, struct FANSI_sgr new) {
  struct FANSI_sgr res = {
    .color=-1, .bg_color=-1, .style=0, .border=0, .ideogram=0, .font=0
  };
  if(old.color > -1 && new.color == - 1) {
    res.color = old.color;
    memcpy(res.color_extra, old.color_extra, 4);
  }
  if(old.bg_color > -1 && new.bg_color == - 1) {
    res.bg_color = old.bg_color;
    memcpy(res.bg_color_extra, old.bg_color_extra, 4);
  }
  if(old.font && !new.font) res.font = old.font;
  res.style = old.style & (~new.style);
  res.border = old.border & (~new.border);
  res.ideogram = old.ideogram & (~new.ideogram);
  return res;
}

// Keep synchronized with `sgr_close`
int FANSI_sgr_active(struct FANSI_sgr sgr) {
  return
    sgr.style || sgr.color >= 0 || sgr.bg_color >= 0 ||
    sgr.font || sgr.border || sgr.ideogram;
}
/*
 * For closing things for substr, so we don't need to automatically normalize
 * every string if we just close with ESC[0m.
 *
 * Pretty inefficient to do it this way...
 *
 * @param x should be a vector of active states at end of strings.
 */
SEXP FANSI_sgr_close_ext(SEXP x, SEXP warn, SEXP term_cap, SEXP norm) {

  if(TYPEOF(x) != STRSXP)
    error("Argument `x` should be a character vector.");  // nocov
  if(TYPEOF(norm) != LGLSXP || XLENGTH(norm) != 1)
    error("Argument `normalize` should be TRUE or FALSE.");  // nocov

  int prt = 0;
  R_xlen_t len = xlength(x);
  SEXP res = PROTECT(allocVector(STRSXP, len)); ++prt;

  PROTECT_INDEX ipx;
  // reserve spot if we need to alloc later
  PROTECT_WITH_INDEX(res, &ipx); ++prt;

  struct FANSI_buff buff;
  FANSI_INIT_BUFF(&buff);
  int normalize = asInteger(norm);

  SEXP R_true = PROTECT(ScalarLogical(1)); ++prt;
  SEXP R_one = PROTECT(ScalarInteger(1)); ++prt;
  SEXP R_zero = PROTECT(ScalarInteger(0)); ++prt;

  for(R_xlen_t i = 0; i < len; ++i) {
    FANSI_interrupt(i);
    SEXP x_chr = STRING_ELT(x, i);
    if(x_chr == NA_STRING || !LENGTH(x_chr)) continue;

    struct FANSI_state state = FANSI_state_init_full(
      x, warn, term_cap, R_true, R_true, R_zero, R_one, i
    );
    while(*(state.string + state.pos_byte)) {
      state = FANSI_read_next(state, i);
    }
    int len = 0;
    char * buff_track = NULL;
    len = FANSI_W_sgr_close(&buff_track, state.sgr, len, normalize, i);
    if(len) {
      if(res == x) REPROTECT(res = duplicate(x), ipx);
      FANSI_size_buff(&buff, len);
      buff_track = buff.buff;
      FANSI_W_sgr_close(&buff_track, state.sgr, len, normalize, i);
      cetype_t chr_type = getCharCE(x_chr);
      SEXP reschr =
        PROTECT(FANSI_mkChar(buff.buff, buff.buff + len, chr_type, i));
      SET_STRING_ELT(res, i, reschr);
      UNPROTECT(1);
    }
  }
  FANSI_release_buff(&buff, 1);
  UNPROTECT(prt);
  return res;
}

/*
 * R interface for state_at_position
 *
 * @param string we're interested in state of
 * @param pos integer positions along the string, zero-index
 * @param keep TRUE or FALSE, whether to keep partial characters at
 *   requested position.
 *
 * No carry param, that should be R-level.
 */

SEXP FANSI_state_at_pos_ext(
  SEXP x, SEXP pos, SEXP type,
  SEXP keep, SEXP is_start, SEXP warn,
  SEXP term_cap, SEXP ctl, SEXP norm,
  SEXP ids
) {
  /*******************************************\
  * IMPORTANT: INPUT MUST ALREADY BE IN UTF8! *
  \*******************************************/

  // errors shoudl be handled R side, but just in case
  if(XLENGTH(x) != 1 || STRING_ELT(x, 0) == NA_STRING)
    error("Argument `x` must be scalar character and not be NA.");   // nocov
  if(
      TYPEOF(norm) != LGLSXP || XLENGTH(norm) != 1 ||
      asLogical(norm) == NA_LOGICAL
    )
    error("Argument `normalize` must be TRUE or FALSE.");      // nocov
  if(TYPEOF(pos) != INTSXP)
    error("Argument `pos` must be integer.");          // nocov
  if(TYPEOF(keep) != LGLSXP)
    error("Argument `keep` must be logical.");          // nocov
  if(XLENGTH(pos) != XLENGTH(keep))
    error("Argument `keep` must be the same length as `pos`.");  // nocov
  if(XLENGTH(pos) != XLENGTH(is_start))
    error("Argument `ends` must be the same length as `pos`."); // nocov
  if(XLENGTH(pos) != XLENGTH(ids))
    error("Argument `ids` must be the same length as `pos`."); // nocov

  int prt = 0;
  SEXP R_true = PROTECT(ScalarLogical(1)); ++prt;
  R_xlen_t len = XLENGTH(pos);
  int normalize = asInteger(norm);

  const int res_cols = 4;  // if change this, need to change rownames init
  if(len > R_XLEN_T_MAX / res_cols) {
    // nocov start
    error("Argument `pos` may be no longer than R_XLEN_T_MAX / %d", res_cols);
    // nocov end
  }
  struct FANSI_state_pair state_pair;

  // Allocate result, will be a res_cols x n matrix.  A bit wasteful to record
  // all the color values given we'll rarely use them, but variable width
  // structures are likely to be much slower.  We could encode most color values
  // into one int but it would be a little annoying to retrieve them

  const char * rownames[4] = { // make sure lines up with res_cols
    "pos.byte", "pos.raw", "pos.ansi", "pos.width"
  };
  SEXP res_rn = PROTECT(allocVector(STRSXP, res_cols)); ++prt;
  for(int i = 0; i < res_cols; i++)
    SET_STRING_ELT(
      res_rn, i,
      FANSI_mkChar(
        rownames[i], rownames[i] + strlen(rownames[i]),
        CE_NATIVE, (R_xlen_t) 0
    ) );

  // Result will comprise a character vector with all the state tags at the
  // position as well as the various position translations in a matrix with as
  // many *columns* as the character vector has elements

  SEXP res_mx = PROTECT(allocVector(INTSXP, res_cols * len)); ++prt;
  SEXP dim = PROTECT(allocVector(INTSXP, 2)); ++prt;
  SEXP dim_names = PROTECT(allocVector(VECSXP, 2)); ++prt;

  INTEGER(dim)[0] = res_cols;
  INTEGER(dim)[1] = len;
  setAttrib(res_mx, R_DimSymbol, dim);
  SET_VECTOR_ELT(dim_names, 0, res_rn);
  SET_VECTOR_ELT(dim_names, 1, R_NilValue);
  setAttrib(res_mx, R_DimNamesSymbol, dim_names);

  SEXP res_str = PROTECT(allocVector(STRSXP, len)); ++prt;
  const char * empty = "";
  SEXP res_chr, res_chr_prev =
    PROTECT(FANSI_mkChar(empty, empty, CE_NATIVE, (R_xlen_t) 0)); ++prt;
  struct FANSI_state state = FANSI_state_init_full(
    x, warn, term_cap, R_true, R_true, type, ctl, (R_xlen_t) 0
  );
  struct FANSI_state state_prev = state;

  // Compute state at each `pos` and record result in our results matrix

  int type_int = asInteger(type);
  int pos_prev = -1;
  int * pos_i = INTEGER(pos);
  int * keep_i = LOGICAL(keep);
  int * start_i = INTEGER(is_start);
  struct FANSI_buff buff;
  int * res_mx_i = INTEGER(res_mx);
  FANSI_INIT_BUFF(&buff);

  // Need to handle possibility of real indices; these should always be
  // representable as R_xlen_t as they come from seq_len.
  union i_or_d_p {int * i; double * d;};
  union i_or_d_p id_i_p;

  if(TYPEOF(ids) == INTSXP) id_i_p.i = INTEGER(ids);
  else if(TYPEOF(ids) == REALSXP) id_i_p.d = REAL(ids);
  else error("Internal Error: bad ID type.");

  for(R_xlen_t i = 0; i < len; i++) {
    FANSI_interrupt(i);
    if(pos_i[i] == NA_INTEGER) {
      error("Internal Error: NAs not allowed"); // nocov
    } else {
      if(pos_i[i] < pos_prev)
        // nocov start
        error("Internal Error: `pos` must be sorted %d %d.", pos_i[i], pos_prev);
        // nocov end

      R_xlen_t id_i;
      id_i = (R_xlen_t)(TYPEOF(ids) == INTSXP ? id_i_p.i[i] : id_i_p.d[i]);

      // We need to allow the same position multiple times in case it shows up
      // as starts and ends, etc.

      if(pos_i[i] == pos_prev) state_pair.cur = state_pair.prev;

      state_pair =
        state_at_pos2(pos_i[i], state_prev, type_int, keep_i[i], id_i);
      state = state_pair.cur;
      state_prev = state_pair.prev;

      // Record position, these will be set back to 1 index at the R-level
      // by adding 1 to the starts.

      if(state.pos_ansi == FANSI_lim.lim_int.max && start_i[i]) {
        // nocov start
        error(
          "Internal Error: integer overflow for start position at index[%td]",
          id_i  // no FANSI_ind b/c already in 1 based index.
        );
        // nocov end
      }
      res_mx_i[i * res_cols + 0] = state.pos_byte;
      res_mx_i[i * res_cols + 1] = state.pos_raw;
      res_mx_i[i * res_cols + 2] = state.pos_ansi;  // this is what's used
      res_mx_i[i * res_cols + 3] = state.pos_width_target;

      // Record color tag if state changed

      if(FANSI_sgr_comp(state.sgr, state_prev.sgr)) {
        // this computes length twice..., we know state_char can be at most
        // INT_MAX excluding NULL (and certainly will be much less).
        char * state_chr = FANSI_sgr_as_chr(&buff, state.sgr, normalize, i);
        res_chr = PROTECT(
          FANSI_mkChar(
            state_chr, state_chr + strlen(state_chr), CE_NATIVE, i
        ) );
      } else {
        res_chr = PROTECT(res_chr_prev);
      }
      SET_STRING_ELT(res_str, i, res_chr);
      res_chr_prev = res_chr;
      UNPROTECT(1);  // note res_chr is protected by virtue of being in res_str
      pos_prev = pos_i[i];
    }
    state_prev = state;
  }
  FANSI_release_buff(&buff, 1);

  SEXP res_list = PROTECT(allocVector(VECSXP, 2)); ++prt;
  SET_VECTOR_ELT(res_list, 0, res_str);
  SET_VECTOR_ELT(res_list, 1, res_mx);

  UNPROTECT(prt);
  return(res_list);
}
