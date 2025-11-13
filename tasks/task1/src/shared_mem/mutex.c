// Решение проблемы доступа к общей переменной с помощью мьютексов

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>

#define NumThreads      16

volatile int     var1;
volatile int     var2;
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
volatile int    running = 1;  // Флаг для graceful shutdown

void    *update_thread (void *);
char    *progname = "mutex";

int main ()
{
    pthread_t threadID [NumThreads];
    int i;
    
    setvbuf (stdout, NULL, _IOLBF, 0);
    var1 = var2 = 0;
    printf ("%s:  starting; creating threads\n", progname);

    for (i = 0; i < NumThreads; i++) {
        pthread_create (&threadID [i], NULL, update_thread, (void *)(long)i);
    }

    sleep (10);
    printf ("%s:  stopping; cancelling threads\n", progname);
    
    // Graceful shutdown - устанавливаем флаг вместо cancel
    running = 0; // Потоки сами завершатся при следующей проверке условия
    
    // Ждем завершения потоков
    for (i = 0; i < NumThreads; i++) {
        pthread_join(threadID [i], NULL);
    }
    
    printf ("%s:  all done, var1 is %d, var2 is %d\n", progname, var1, var2);
    pthread_mutex_destroy(&mutex);
    exit (0);
}

void *update_thread (void *i)
{
    while (running) {  // Проверяем флаг вместо бесконечного цикла
        pthread_mutex_lock(&mutex); // блокируем доступ к общим переменным
        
        // Оба инкремента выполняются как одна неделимая операция
        for (int j = 0; j < 10000; j++) {
            var1++;
            var2++; // В защищенной версии var1 и var2 всегда равны!
        }
        
        pthread_mutex_unlock(&mutex); // освобождаем доступ
        
        // Небольшая пауза чтобы не грузить CPU полностью
        usleep(1000);
    }
    return (NULL);
}