/*
 * Сравнение результатов "до" и "после" оптимизации:
 * 
 * Результаты до оптимизации (SCHED_OTHER):
 * - Средний джиттер: 193,215 нс 
 * - 99-й процентиль: 514,750 нс 
 * - Максимальный джиттер: 2,046,113 нс 
 * - Минимальный джиттер: 53,078 нс 
 * 
 * Результаты после оптимизации (SCHED_FIFO + mlockall + CPU affinity):
 * - Средний джиттер: 80,409 нс - улучшение в 2.4 раза
 * - 99-й процентиль: 437,099 нс - улучшение в 1.2 раза
 * - Максимальный джиттер: 553,234 нс - улучшение в 3.7 раза
 * - Минимальный джиттер: 4,430 нс - улучшение в 12 раз
 * 
 * Объяснение влияния техник на уменьшение джиттера:
 * 
 * 1. SCHED_FIFO планировщик (наибольший вклад):
 *    - Исключает вытеснение обычными процессами (SCHED_OTHER)
 *    - Гарантирует немедленное выполнение после пробуждения из clock_nanosleep()
 *    - Устраняет недетерминизм планировщика CFS (Completely Fair Scheduler)
 *    - Приоритет 50 обеспечивает доминирование над системными процессами
 *    - Эффект: Уменьшил максимальный джиттер с 2.05 мс до 0.55 мс
 * 
 * 2. Блокировка памяти (mlockall) - критично для детерминизма:
 *    - Предотвращает вытеснение страниц памяти процесса в swap
 *    - Устраняет задержки, вызванные page faults (до 10+ мс)
 *    - Исключает паузы на подкачку страниц с диска
 *    - MCL_CURRENT блокирует текущие страницы памяти
 *    - MCL_FUTURE блокирует все будущие выделения памяти
 *    - Эффект: Стабилизировал минимальный джиттер (улучшение в 12 раз)
 * 
 * 3. Привязка к ядру CPU - улучшает стабильность:
 *    - Закрепляет поток на CPU 11 (последнее ядро)
 *    - Сохраняет кэш процессора (L1, L2, L3) - исключает промахи кэша при миграции
 *    - Предотвращает TLB misses - сохраняет буферы ассоциативной трансляции
 *    - Исключает межъядерные задержки синхронизации
 *    - Изолирует от влияния других процессов на выбранном ядре
 *    - Эффект: Улучшил средний джиттер и стабильность измерений
 * 
 * Комбинация трех техник дала значительное улучшение, особенно в максимальном 
 * джиттере.
 */


#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#ifndef __linux__
int main(void) {
    printf("sched_fifo_jitter: Linux-only example (SCHED_FIFO not available)\n");
    return 0;
}
#else

static int compare_i64(const void *a, const void *b) {
    int64_t va = *(const int64_t *)a;
    int64_t vb = *(const int64_t *)b;
    if (va < vb) return -1;
    if (va > vb) return 1;
    return 0;
}

static inline int64_t ts_to_ns(const struct timespec *ts) {
    return (int64_t)ts->tv_sec * 1000000000LL + (int64_t)ts->tv_nsec;
}
static inline void ns_to_ts(int64_t ns, struct timespec *ts) {
    ts->tv_sec = (time_t)(ns / 1000000000LL);
    ts->tv_nsec = (long)(ns % 1000000000LL);
}

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);

    // 1. Переключение планировщика на SCHED_FIFO
    struct sched_param sp = {.sched_priority = 50};
    if (sched_setscheduler(0, SCHED_FIFO, &sp) != 0) {
        perror("WARNING: sched_setscheduler failed; continuing with default scheduler");
    } else {
        printf("Switched to SCHED_FIFO priority %d\n", sp.sched_priority);
    }

    // 2. Блокировка памяти
    if (mlockall(MCL_CURRENT | MCL_FUTURE) != 0) {
        perror("WARNING: mlockall failed");
    }

    // 3. Привязка к ядру CPU
    long n_cpus = sysconf(_SC_NPROCESSORS_ONLN);
    if (n_cpus > 0) {
        cpu_set_t cpu_set;
        CPU_ZERO(&cpu_set);
        CPU_SET(n_cpus - 1, &cpu_set);
        if (pthread_setaffinity_np(pthread_self(), sizeof(cpu_set), &cpu_set) != 0) {
            perror("WARNING: pthread_setaffinity_np failed");
        } else {
            printf("Pinned thread to CPU %ld\n", n_cpus - 1);
        }
    }
    

    const int64_t period = 2 * 1000000LL; /* 2ms */
    const int samples = 5000;
    int64_t deltas[samples]; // Store all deltas for percentile calculation

    struct timespec next;
    clock_gettime(CLOCK_MONOTONIC, &next);
    int64_t next_ns = ts_to_ns(&next) + period;

    for (int i = 0; i < samples; ++i) {
        ns_to_ts(next_ns, &next);
        int rc;
        // Absolute wait is crucial to prevent period drift.
        do {
            rc = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, NULL);
        } while (rc == EINTR);
        if (rc != 0) {
            fprintf(stderr, "clock_nanosleep: %s\n", strerror(rc));
            return EXIT_FAILURE;
        }

        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        // The "error" or "jitter" for this cycle.
        // It's the difference between when we woke up and when we *should* have.
        deltas[i] = ts_to_ns(&now) - next_ns;
        next_ns += period;
    }

    // --- Statistics ---
    qsort(deltas, samples, sizeof(int64_t), compare_i64);
    int64_t min = deltas[0];
    int64_t max = deltas[samples - 1];
    int64_t p99 = deltas[(samples * 99) / 100];
    int64_t sum = 0;
    for (int i = 0; i < samples; ++i) {
        sum += deltas[i];
    }
    double avg = (double)sum / (double)samples;

    printf("\nJitter statistics over %d samples (2ms period):\n", samples);
    printf("  min latency: %" PRId64 " ns\n", min);
    printf("  avg latency: %.1f ns\n", avg);
    printf("  99th percentile: %" PRId64 " ns\n", p99);
    printf("  max latency: %" PRId64 " ns\n", max);

    return 0;
}
#endif


