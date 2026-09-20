#ifndef TASKS_PIVOT_H
#define TASKS_PIVOT_H

#include "msgpack.h"
#include <stdint.h>

/// Pivot command handlers

/// COMMAND_LINK — needs cmd_id as the pivot identifier
int task_link_with_id(uint32_t cmd_id, const uint8_t *data, uint32_t data_len, mp_writer_t *w);

/// COMMAND_UNLINK
int task_unlink(const uint8_t *data, uint32_t data_len, mp_writer_t *w);

/// COMMAND_PIVOT_EXEC — relay data to child agent
int task_pivot_exec(const uint8_t *data, uint32_t data_len, mp_writer_t *w);

/// COMMAND_PIVOT_LISTEN — reverse pivot: listen for children that dial IN.
/// port == 0 stops listening.
int task_pivot_listen(const uint8_t *data, uint32_t data_len, mp_writer_t *w);

#endif /* TASKS_PIVOT_H */
