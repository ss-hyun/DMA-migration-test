#ifndef HEMEM_TIMER_H
#define HEMEM_TIMER_H

/* Returns the number of seconds encoded in T, a "struct timeval". */
#define tv_to_double(t) (t.tv_sec + (t.tv_usec / 1000000.0))
#define cal_tv(t) (t.tv_sec*1000000 + t.tv_usec)

void timeDiff(struct timeval *d, struct timeval *a, struct timeval *b);
double elapsed(struct timeval *starttime, struct timeval *endtime);
long elapsed_long(struct timeval *starttime, struct timeval *endtime);
long clock_time_elapsed(struct timespec start, struct timespec end);

#endif /* HEMEM_TIMER_H */
