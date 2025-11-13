/*
 *  Objectives:
 *
 *      condvar.c was a two-state example.  The producer (or
 *      state 0) did something which caused the consumer (or
 *      state 1) to run.  State 1 did something which caused
 *      a return to state 0.  Each thread implemented one of
 *      the states.
 *
 *      This example will have 4 states in its state machine 
 *      with the following state transitions:
 *
 *        State 0 -> State 1
 *        State 1 -> State 2 if state 1's internal variable indicates "even"
 *        State 1 -> State 3 if state 1's internal variable indicates "odd"
 *        State 2 -> State 0
 *        State 3 -> State 0
 *    
 *      And, of course, one thread implementing each state, sharing the
 *      same state variable and condition variable for notification of
 *      change in the state variable.
 *
 */ 
#include <stdio.h>
 #include <unistd.h>
 #include <pthread.h>
 #include <sched.h>
 
 volatile int counter = 0;
 volatile int        state;          // which state we are in
 pthread_mutex_t     mutex = PTHREAD_MUTEX_INITIALIZER;
 pthread_cond_t      cond  = PTHREAD_COND_INITIALIZER;
 void    *state_0 (void *);
 void    *state_1 (void *);
 void    *state_2 (void *);
 void    *state_3 (void *);
 char    *progname = "condvar";
 
 int main ()
 {
     setvbuf (stdout, NULL, _IOLBF, 0);
     state = 0;
     pthread_t t_state0, t_state1, t_state2, t_state3 ;
     pthread_create (&t_state0, NULL, state_0, NULL);
     pthread_create (&t_state1, NULL, state_1, NULL);
     pthread_create (&t_state2, NULL, state_2, NULL);
     pthread_create (&t_state3, NULL, state_3, NULL);
     sleep (20); 
    


     printf ("%s:  main, exiting\n", progname);
     return 0;
 }
 
 /*
  *  Обработчик состояния 0 (was producer)
 */
 
 void *state_0 (void *arg)
 {
     while (1) {
        pthread_mutex_lock (&mutex);
        while (state != 0) {
            pthread_cond_wait (&cond, &mutex);
        }
        printf ("%s:  transit 0 -> 1\n", progname);
        state = 1;
        counter++;
        usleep(100 * 1000);
        pthread_cond_broadcast (&cond);
        pthread_mutex_unlock (&mutex);
    }
    return (NULL);
 }
 
 /*
  *  Обработчик состояния 1 
 */
 
 void *state_1 (void *arg)
 {
    while (1) {
        pthread_mutex_lock (&mutex);

        while (state != 1) {
            pthread_cond_wait (&cond, &mutex);
        }

        if(counter % 2 == 0){
            printf ("%s:  transit 1 -> 2\n", progname);
            state = 2;
        }

        else{
            printf ("%s:  transit 1 -> 3\n", progname);
            state = 3;
        }

        counter++;
        usleep(100 * 1000);
        pthread_cond_broadcast (&cond);
        pthread_mutex_unlock (&mutex);
    }
    return (NULL);
 }

 /*
  *  Обработчик состояния 2 (was consumer)
 */

 void *state_2 (void *arg)
 {
    while (1) {
        pthread_mutex_lock (&mutex);
        while (state != 2) {
            pthread_cond_wait (&cond, &mutex);
        }

        printf ("%s:  transit 2 -> 0\n", progname);
        state = 0;

        counter++;
        usleep(100 * 1000);
        pthread_cond_broadcast (&cond);
        pthread_mutex_unlock (&mutex);
    }
    return (NULL);
 }
 
 /*
  *  Обработчик состояния 3 (was consumer)
 */
 
 void *state_3 (void *arg)
 {
     while (1) {
        pthread_mutex_lock (&mutex);
        while (state != 3) {
            pthread_cond_wait (&cond, &mutex);
        }
        printf ("%s:  transit 3 -> 0\n", progname);
        state = 0;
        counter++;
        usleep(100 * 1000);
        pthread_cond_broadcast (&cond);
        pthread_mutex_unlock (&mutex);
    }
    return (NULL);
 }