//  Демонстрация проблемы, когда несколько потоков пытаются получить доступ к общей переменной

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <sched.h>
#include <unistd.h>

#define NumThreads      16

volatile int     var1;
volatile int     var2;

void    *update_thread (void *);
void    do_work(void);
char    *progname = "nomutex";

int main ()
{
    pthread_t           threadID [NumThreads];
    pthread_attr_t      attrib;
    struct sched_param  param;
    int                 i, policy;
    
    setvbuf (stdout, NULL, _IOLBF, 0);
    var1 = var2 = 0;
    printf ("%s:  starting; creating threads\n", progname);

    // УПРОЩАЕМ: убираем сложные атрибуты для большей производительности
    pthread_attr_init (&attrib);

    for (i = 0; i < NumThreads; i++) {
        pthread_create (&threadID [i], NULL, update_thread, (void *)(long)i);
    }

    sleep (10);  // Уменьшаем время до 10 секунд
    printf ("%s:  stopping; cancelling threads\n", progname);
    for (i = 0; i < NumThreads; i++) {
        pthread_cancel (threadID [i]);
    }
    printf ("%s:  all done, var1 is %d, var2 is %d\n", progname, var1, var2);
    exit (0);
}

void do_work()
{
    // Пустая функция -  для совместимости с оригинальным кодом
}

void *update_thread (void *i)
{
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
    
    while (1) {
        // убираем проверку и исправление - пусть переменные рассинхронизируются!
        // if (var1 != var2) {
        //     var1 = var2;
        // }
        
        // Ммаксимально усиливаем гонку - тысячи операций
        for (int j = 0; j < 10000; j++) {
            var1++;
            // Убираем sched_yield для максимальной скорости
            var2++;
        }
    }
    return (NULL);
}