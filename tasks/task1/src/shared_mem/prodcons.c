/*
 *  Демонстрация POSIX условных переменных на примере "Производитель и потребитель".
 *  Так как у нас всего два потока, ожидающих сигнала,    
 *  в любой момент работы одного из них мы можем просто использовать вызов
 *  pthread_cond_signal для пробуждения второго потока.
 *
*/

#include <stdio.h>
#include <pthread.h>
#include <sched.h>
#include <unistd.h>
#include <signal.h>

// mutex и условная переменная
pthread_mutex_t     mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t      cond  = PTHREAD_COND_INITIALIZER;
volatile int        state = 0;    // переменная состояния
volatile int        product = 0;  // вывод производителя
volatile int        shutdown = 0; // флаг завершения - для корректного завершения потоков

void    *producer (void *);
void    *consumer (void *);
void    do_producer_work (void);
void    do_consumer_work (void);
char    *progname = "prodcons";

int main ()
{
  pthread_t producer_thread, consumer_thread; // сохраняем идентификаторы потоков
  
  setvbuf (stdout, NULL, _IOLBF, 0);
  pthread_create (&producer_thread, NULL, producer, NULL);  // сохраняем ID потока
  pthread_create (&consumer_thread, NULL, consumer, NULL);  // сохраняем ID потока
  
  sleep (20);     // Позволим потокам выполнить "работу"
  
  // Корректное завершение потоков
  pthread_mutex_lock (&mutex);
  shutdown = 1; // устанавливаем флаг завершения
  pthread_cond_broadcast (&cond); // будим все ожидающие потоки
  pthread_mutex_unlock (&mutex);
  
  // Ожидаем завершения рабочих потоков
  pthread_join (producer_thread, NULL);
  pthread_join (consumer_thread, NULL);
  
  printf ("%s:  main, exiting\n", progname);
  return 0;
}

// Производитель
void *producer (void *arg)
{
  while (1) {
    pthread_mutex_lock (&mutex);
    // добавляем проверку флага завершения к условию ожидания
    while (state == 1 && !shutdown) {
      pthread_cond_wait (&cond, &mutex);
    }
    
    // проверяем флаг завершения и выходим из цикла
    if (shutdown) {
      pthread_mutex_unlock (&mutex);
      break;
    }
    
    printf ("%s:  produced %d, state %d\n", progname, ++product, state);
    state = 1;
    pthread_cond_signal (&cond);
    pthread_mutex_unlock (&mutex);
    do_producer_work ();
  }
  printf ("%s: producer exiting\n", progname); // сообщение о завершении
  return (NULL);
}

// Потребитель
void *consumer (void *arg)
{
  while (1) {
    pthread_mutex_lock (&mutex);
    // добавляем проверку флага завершения к условию ожидания
    while (state == 0 && !shutdown) {
      pthread_cond_wait (&cond, &mutex);
    }
    
    // проверяем флаг завершения и выходим из цикла
    if (shutdown) {
      pthread_mutex_unlock (&mutex);
      break;
    }
    
    printf ("%s:  consumed %d, state %d\n", progname, product, state);
    state = 0;
    pthread_cond_signal (&cond);
    pthread_mutex_unlock (&mutex);
    do_consumer_work ();
  }
  printf ("%s: consumer exiting\n", progname); // сообщение о завершении
  return (NULL);
}

void do_producer_work (void)
{
  usleep (100 * 1000); // имитация работы производителя
}

void do_consumer_work (void)
{
  usleep (100 * 1000); // имитация работы потребителя
}