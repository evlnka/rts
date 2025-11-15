/*
 * POSIX clock demo with 2 ms period sampling for Linux.
 *
 * Цели:
 * - Показать использование CLOCK_MONOTONIC и clock_getres
 * - Реализовать периодическую выборку с шагом 2 мс через
 *   абсолютный clock_nanosleep(TIMER_ABSTIME)
 * - Измерить фактические дельты между сэмплами и вывести статистику
 *
 * Критическая важность TIMER_ABSTIME:
 * TIMER_ABSTIME гарантирует, что каждый следующий момент пробуждения вычисляется
 * от начального эталонного времени, а не от момента фактического пробуждения.
 * Это предотвращает накопление ошибок (дрейф таймера), которые возникают при
 * использовании относительных интервалов, где каждая итерация добавляет свою
 * ошибку сна к следующей. С ABSOLUTE время пробуждения остается привязанным
 * к идеальной временной сетке, а небольшие задержки не влияют на будущие периоды.
 * 
 * Без TIMER_ABSTIME: 2ms + error1 + 2ms + error2 + ... = накопление ошибки
 * С TIMER_ABSTIME: всегда пробуждение в T0+2ms, T0+4ms, T0+6ms (идеальная сетка)
 */

#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <stdint.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define BILLION 1000000000LL
#define MILLION 1000000LL
#define NUM_SAMPLES 5000 // 5000 * 2 ms ≈ 10 секунд эксперимента 

static inline int64_t timespec_to_ns(const struct timespec *ts) {
    return (int64_t)ts->tv_sec * BILLION + (int64_t)ts->tv_nsec;
}

static inline void ns_to_timespec(int64_t ns, struct timespec *ts) {
    ts->tv_sec = (time_t)(ns / BILLION);
    ts->tv_nsec = (long)(ns % BILLION);
}

#ifdef __linux__
int main(void) {
    struct timespec res_rt = {0}, res_mono = {0};
    struct timespec t_next = {0}, now = {0}, prev = {0};
    const int64_t period_ns = 2 * MILLION; // 2 ms 
    int64_t deltas_ns[NUM_SAMPLES];
    int samples = 0;

    setvbuf(stdout, NULL, _IOLBF, 0);

    if (clock_getres(CLOCK_REALTIME, &res_rt) != 0) {
        fprintf(stderr, "clock_getres(CLOCK_REALTIME) failed: %s\n", strerror(errno));
        return EXIT_FAILURE;
    }
    if (clock_getres(CLOCK_MONOTONIC, &res_mono) != 0) {
        fprintf(stderr, "clock_getres(CLOCK_MONOTONIC) failed: %s\n", strerror(errno));
        return EXIT_FAILURE;
    }

    printf("Resolution: REALTIME=%ld ns, MONOTONIC=%ld ns\n",
           (long)res_rt.tv_nsec, (long)res_mono.tv_nsec);

    // Получаем начальное время для первого пробуждения
    if (clock_gettime(CLOCK_MONOTONIC, &prev) != 0) {
        fprintf(stderr, "clock_gettime failed: %s\n", strerror(errno));
        return EXIT_FAILURE;
    }

    int64_t next_ns = timespec_to_ns(&prev) + period_ns; /* стартуем через один период */
    
    for (samples = 0; samples < NUM_SAMPLES; ++samples) {
        ns_to_timespec(next_ns, &t_next);

        /* Абсолютный сон до t_next: устойчив к дрейфу */
        int rc;
        do {
            rc = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &t_next, NULL);
        } while (rc == EINTR);
        if (rc != 0) {
            fprintf(stderr, "clock_nanosleep failed: %s\n", strerror(rc));
            return EXIT_FAILURE;
        }

        // Измеряем фактическое время пробуждения
        if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
            fprintf(stderr, "clock_gettime failed: %s\n", strerror(errno));
            return EXIT_FAILURE;
        }

        // ПРАВИЛЬНЫЙ расчет дельты: время между последовательными фактическими пробуждениями
        int64_t now_ns = timespec_to_ns(&now);
        int64_t prev_ns = timespec_to_ns(&prev);
        
        if (samples > 0) {
            deltas_ns[samples] = now_ns - prev_ns;
        }
        
        // Сохраняем текущее время как предыдущее для следующей итерации
        prev = now;
        
        // Вычисляем следующий абсолютный момент пробуждения
        next_ns += period_ns;
    }

    /* Статистика (исключаем sample 0, так как у него нет предыдущего значения) */
    int64_t min_ns = INT64_MAX, max_ns = INT64_MIN, sum_ns = 0;
    int valid_samples = NUM_SAMPLES - 1;
    
    for (int i = 1; i < NUM_SAMPLES; ++i) {
        if (deltas_ns[i] < min_ns) min_ns = deltas_ns[i];
        if (deltas_ns[i] > max_ns) max_ns = deltas_ns[i];
        sum_ns += deltas_ns[i];
    }
    double avg_ns = (double)sum_ns / (double)valid_samples;

    // Расчет стандартного отклонения для оценки стабильности периода
    double sum_sq_diff = 0;
    for (int i = 1; i < NUM_SAMPLES; ++i) {
        sum_sq_diff += pow((double)deltas_ns[i] - avg_ns, 2);
    }
    double std_dev_ns = sqrt(sum_sq_diff / valid_samples);

    printf("Period stats over %d samples (target: %" PRId64 " ns):\n", valid_samples, period_ns);
    printf("  min=%" PRId64 " ns, avg=%.1f ns, max=%" PRId64 " ns, std_dev=%.1f ns\n",
           min_ns, avg_ns, max_ns, std_dev_ns);

    /* Вывести первые несколько измерений для наглядности */
    printf("\nFirst 10 samples (delta from previous actual wakeup, ns):\n");
    for (int i = 1; i < 11 && i < NUM_SAMPLES; ++i) {
        printf("  sample %d: %" PRId64 "\n", i, deltas_ns[i]);
    }

    return EXIT_SUCCESS;
}
#else
int main(void) {
    struct timespec res_rt = {0};
    const long period_ns = 2 * 1000000L; /* 2 ms */
    struct timespec req;
    struct timespec start, prev, now;
    const int num_samples = 5000;
    long min_ns = 999999999L, max_ns = 0; long long sum_ns = 0;

    setvbuf(stdout, NULL, _IOLBF, 0);
    clock_getres(CLOCK_REALTIME, &res_rt);
    printf("Resolution (CLOCK_REALTIME) ~ %ld ns (emulated periodic sleep)\n", res_rt.tv_nsec);
    clock_gettime(CLOCK_REALTIME, &start);
    prev = start;

    for (int i = 0; i < num_samples; ++i) {
        req.tv_sec = 0; req.tv_nsec = period_ns;
        nanosleep(&req, NULL);
        clock_gettime(CLOCK_REALTIME, &now);
        long delta = (long)((now.tv_sec - prev.tv_sec) * 1000000000LL + (now.tv_nsec - prev.tv_nsec));
        if (delta < min_ns) min_ns = delta;
        if (delta > max_ns) max_ns = delta;
        sum_ns += delta;
        prev = now;
    }
    double avg = (double)sum_ns / (double)num_samples;
    printf("2ms-period stats over %d samples (relative_sleep): min=%ld ns, avg=%.1f ns, max=%ld ns\n",
           num_samples, min_ns, avg, max_ns);
    return EXIT_SUCCESS;
}
#endif