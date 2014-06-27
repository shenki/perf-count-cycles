/*
 * Measure number of cpu cycles in 1 second.
 *
 * Joel Stanley <joel@jms.id.au>
 *
 * GPL V3
 *
 */
#define _GNU_SOURCE

#include <asm/unistd.h>
#include <errno.h>
#include <linux/perf_event.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <unistd.h>

static int pipefd[2];

long perf_event_open(struct perf_event_attr *hw_event, pid_t pid,
                     int cpu, int group_fd, unsigned long flags)
{
        int ret;

        ret = syscall(__NR_perf_event_open, hw_event, pid, cpu,
                        group_fd, flags);
        return ret;
}


int main(int argc, char **argv)
{
        int counter_fd, child, dummy, rc;
        long cpu;
        unsigned long long count;
        cpu_set_t cpumask;
        struct perf_event_attr attr;

        if (argc != 2) {
                fprintf(stderr, "usage: %s [cpu number]\n", argv[0]);
                exit(EXIT_FAILURE);
        }

        cpu = strtol(argv[1], NULL, 10);
        if (errno) {
                fprintf(stderr, "Invalid cpu number: %s", argv[1]);
                exit(EXIT_FAILURE);
        }

        memset(&attr, 0, sizeof(attr));
        attr.type = PERF_TYPE_HARDWARE;
        attr.config = PERF_COUNT_HW_CPU_CYCLES;
        attr.disabled = 1;
        attr.size = sizeof(attr);
        counter_fd = perf_event_open(&attr, -1, cpu, -1, 0);
        if (counter_fd < 0) {
                perror("perf_event_open");
                exit(EXIT_FAILURE);
        }

        pipe(pipefd);

        child = fork();
        if (child < 0) {
                perror("fork");
                exit(EXIT_FAILURE);
        }

        if (child == 0) {
                read(pipefd[0], &dummy, sizeof(dummy));

                if (sched_getaffinity(0, sizeof(cpumask), &cpumask) < 0)
                        perror("Child call scheld_getaffinty failed\n");

                while (!CPU_ISSET(cpu, &cpumask))
                        cpu++;

                printf("child %d running on cpu %ld\n", getpid(), cpu);
                write(pipefd[0], &dummy, sizeof(dummy));

                while (1);
        }

        /* Parent. */

        CPU_ZERO(&cpumask);
        CPU_SET(cpu, &cpumask);
        if (sched_setaffinity(child, sizeof(cpumask), &cpumask)) {
                perror("sched_setaffinity");
                exit(EXIT_FAILURE);
        }

        /* Tell child affinty has been set. */
        write(pipefd[1], &dummy, sizeof(dummy));

        /* Wait for child to display it's running cpu. */
        read(pipefd[1], &dummy, sizeof(dummy));

        rc = ioctl(counter_fd, PERF_EVENT_IOC_RESET, 0);
        if (rc != 0)
                fprintf(stderr, "PERF_EVENT_IOC_RESET failed: %d\n", rc);

        rc = ioctl(counter_fd, PERF_EVENT_IOC_ENABLE, 0);
        if (rc != 0)
                fprintf(stderr, "PERF_EVENT_IOC_ENABLE failed: %d\n", rc);

        usleep(1000000);

        rc = ioctl(counter_fd, PERF_EVENT_IOC_DISABLE, 0);
        if (rc != 0)
                fprintf(stderr, "PERF_EVENT_IOC_DISABLE failed: %d\n", rc);

        kill(child, SIGTERM);

        if (read(counter_fd, &count, sizeof(count)) < 0) {
                perror("reading fd");
                exit(EXIT_FAILURE);
        }

        printf("Got %llu\n", count);

        return 0;
}
