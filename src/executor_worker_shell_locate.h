#ifndef REMOTIFY_EXECUTOR_WORKER_SHELL_LOCATE_H
#define REMOTIFY_EXECUTOR_WORKER_SHELL_LOCATE_H

typedef enum ExecutorShellType {
    EXECUTOR_SHELL_SH,
    EXECUTOR_SHELL_CMD,
    EXECUTOR_SHELL_POWERSHELL
} ExecutorShellType;

const char *executor_worker_shell_path(void);
ExecutorShellType executor_worker_shell_type(void);

#endif
