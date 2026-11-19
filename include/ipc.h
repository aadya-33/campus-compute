/*
 * ipc.h - FR4: POSIX shared memory + named semaphores between processes.
 */
#ifndef IPC_H
#define IPC_H

/* Parent process creates the shared memory and the named semaphores, forks
 * two writer processes, reads 'msgs_per_writer' messages from each of them
 * and finally removes every IPC object it created.
 * Returns 0 on success. */
int ipc_run(int msgs_per_writer);

/* "ipc" command: argv[0] == "ipc". */
int ipc_cmd(int argc, char **argv);

#endif /* IPC_H */
