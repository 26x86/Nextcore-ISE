/* SPDX-License-Identifier: BSD-4-Clause; independently authored arithmetic. */
#ifndef NEXTCORE_FP_SCALAR_H
#define NEXTCORE_FP_SCALAR_H
#include <stdint.h>

/* Internal prerequisite only: no guest FP state or capability admission. */
typedef enum {
    NC_FP_RN = 0, /* Nearest, ties to even. */
    NC_FP_RP = 1, /* Toward positive infinity. */
    NC_FP_RM = 2, /* Toward negative infinity. */
    NC_FP_RZ = 3  /* Toward zero. */
} nc_fp_rounding;
enum {
    NC_FP_IOC = 1u, NC_FP_DZC = 2u, NC_FP_OFC = 4u,
    NC_FP_UFC = 8u, NC_FP_IXC = 16u,
    NC_FP_STATUS_MASK = 31u
};
typedef struct {
    nc_fp_rounding rounding;
    uint32_t status;
} nc_fp32_state;
typedef struct {
    uint32_t bits;
    uint32_t status; /* Cumulative status, including earlier operations. */
} nc_fp32_result;

void nc_fp32_init(nc_fp32_state *state);
/* Accept only RMode [23:22]; other controls, including enables, are rejected
 * without mutation. AH/FZ/DN are unsupported. Returns one on acceptance. */
int nc_fp32_configure(nc_fp32_state *state, uint32_t control);
/* Clearing flags is explicit and never changes rounding. */
void nc_fp32_clear_status(nc_fp32_state *state, uint32_t mask);
/* Gradual underflow, tininess before rounding, and no exception trapping.
 * State must have been initialized; operands and result are raw binary32. */
nc_fp32_result nc_fp32_add(nc_fp32_state *state, uint32_t a, uint32_t b);
nc_fp32_result nc_fp32_div(nc_fp32_state *state, uint32_t a, uint32_t b);
#endif
