/*
 * Демонстрация периодического таймера с использованием timerfd.
 *
 * Цель: Освоить современный Linux API (timerfd) для создания периодических событий 
 * без использования сигналов.
 *
 * Сценарий:
 * 1. Создать таймер с помощью timerfd_create.
 * 2. Настроить его на первое срабатывание через 5 секунд,
 *    а затем периодически каждые 1.5 секунды (1500 мс).
 * 3. В цикле ожидать событий от таймера с помощью read() и выводить количество 
 *    истечений и текущее время.
 *
 * Сравнение подходов clock_nanosleep и timerfd:
 * 
 * clock_nanosleep:
 * - Блокирующий вызов, поток засыпает до указанного времени
 * - Простая реализация для одиночных периодических задач
 * - Точное время пробуждения с TIMER_ABSTIME
 * - Не интегрируется с другими I/O операциями
 * 
 * timerfd:
 * - Представляет таймер как файловый дескриптор
 * - Неблокирующая интеграция с epoll, select, poll
 * - Можно ожидать несколько таймеров и сокетов одновременно
 * - Автоматическое накопление срабатываний при пропуске
 * - Лучше для сложных приложений с множеством I/O источников
 *
 * timerfd предпочтительнее когда:
 * - Нужно интегрировать таймеры с сетевыми соединениями
 * - Множество таймеров должны обрабатываться в одном потоке
 * - Приложение использует event loop (epoll)
 * - Важно обрабатывать пропущенные срабатывания
 */

#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#ifdef __linux__
#include <sys/timerfd.h>
#endif

static inline int64_t to_ns(const struct timespec *ts) {
    return (int64_t)ts->tv_sec * 1000000000LL + (int64_t)ts->tv_nsec;
}

#ifdef __linux__
int main(void) {
    int tfd;
    struct itimerspec its;
    struct timespec now;
    uint64_t expirations;
    const int iterations_to_run = 5;
    int total_expirations = 0;

    setvbuf(stdout, NULL, _IOLBF, 0);

    // CLOCK_MONOTONIC - лучший выбор для таймеров, т.к. на него не влияет
    // изменение системного времени (NTP, ручная настройка времени).
    // TFD_CLOEXEC - дескриптор автоматически закрывается при exec()
    tfd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC);
    if (tfd == -1) {
        perror("timerfd_create failed");
        return EXIT_FAILURE;
    }

    // Настройка времени: первое срабатывание через 5 секунд
    its.it_value.tv_sec = 5;
    its.it_value.tv_nsec = 0;

    // Настройка периода: 1500 мс (1 секунда + 500 млн наносекунд)
    its.it_interval.tv_sec = 1;
    its.it_interval.tv_nsec = 500000000;

    printf("Timer configured:\n");
    printf("  First expiration: 5 seconds\n");
    printf("  Periodic interval: 1500 ms\n");
    printf("  Waiting for %d expirations...\n\n", iterations_to_run);

    if (timerfd_settime(tfd, 0, &its, NULL) == -1) {
        perror("timerfd_settime failed");
        close(tfd);
        return EXIT_FAILURE;
    }

    for (int i = 0; i < iterations_to_run; ++i) {
        // read() блокируется, пока таймер не сработает.
        // Возвращает количество истечений таймера (обычно 1, но может быть больше при пропуске)
        ssize_t rd = read(tfd, &expirations, sizeof(expirations));
        if (rd < 0) {
            if (errno == EINTR) {
                // Прервано сигналом, продолжаем ожидание
                --i;
                continue;
            }
            perror("read(timerfd) failed");
            close(tfd);
            return EXIT_FAILURE;
        }

        if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
            perror("clock_gettime failed");
            close(tfd);
            return EXIT_FAILURE;
        }

        total_expirations += expirations;
        
        printf("Timer expiration #%d:\n", i + 1);
        printf("  Current time:    [%ld.%09ld] seconds\n", now.tv_sec, now.tv_nsec);
        printf("  Expirations:     %" PRIu64 " (this read)\n", expirations);
        printf("  Total expirations: %d\n\n", total_expirations);
        
        // Если было несколько срабатываний, выводим предупреждение
        if (expirations > 1) {
            printf("  NOTE: Missed %" PRIu64 " timer expirations!\n\n", expirations - 1);
        }
    }

    printf("Timer demo completed. Total expirations: %d\n", total_expirations);
    close(tfd);
    return EXIT_SUCCESS;
}
#else
int main(void) {
    printf("reptimer_timerfd: Linux-only example (timerfd not available on this platform)\n");
    return 0;
}
#endif