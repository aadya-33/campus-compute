/*
 * logger.h - FR3: concurrent activity logger (bounded producer-consumer queue).
 */
#ifndef LOGGER_H
#define LOGGER_H

#define LOG_MAX_CAP 64
#define LOG_DEFAULT_FILE "logs/campuscompute.log"

/* Run the logger: 'producers' threads each create 'items' events, 'consumers'
 * threads take events from a queue of size 'cap' and write them to 'logfile'.
 * Returns 0 if every event was produced, consumed and logged exactly once. */
int logger_run(int producers, int consumers, int items, int cap, const char *logfile);

/* "logger" command: argv[0] == "logger". */
int logger_cmd(int argc, char **argv);

#endif /* LOGGER_H */
