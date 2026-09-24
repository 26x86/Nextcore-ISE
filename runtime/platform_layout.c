/* SPDX-License-Identifier: BSD-4-Clause */
#include "boot_jit.h"
#include <stdio.h>
int main(void) {
    printf("{\"options_size\":%zu,\"options_align\":%zu,\"options_override\":%zu,\"options_vbar\":%zu,"
      "\"result_size\":%zu,\"result_align\":%zu,\"result_override\":%zu,\"result_pending\":%zu,\"result_elr\":%zu,\"result_sp\":%zu}\n",
      sizeof(vf_boot_options_v2),_Alignof(vf_boot_options_v2),
      offsetof(vf_boot_options_v2,initial_override),offsetof(vf_boot_options_v2,vbar),
      sizeof(vf_boot_result_v2),_Alignof(vf_boot_result_v2),
      offsetof(vf_boot_result_v2,platform_override),offsetof(vf_boot_result_v2,pending_lines),
      offsetof(vf_boot_result_v2,elr),offsetof(vf_boot_result_v2,sp));
}
