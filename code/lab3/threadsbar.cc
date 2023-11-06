// threadsbar.cc
//	C++ version of n threads barrier problem.
//	Ref. "The Little Book of Semaphores V2.2.1 -Allen B. Downey 2016.pdf" 3.6.4
//  Ref. "OS22 Ch06 Process Synchronization.pptx"

#include <unistd.h>
#include <stdio.h>

#include "copyright.h"
#include "system.h"
#include "synch.h"

#define N_THREADS  10    // the number of threads
#define MAX_NAME   16    // the maximum lengh of a name
#define N_TICKS    1000  // the number of ticks to advance simulated time

Thread *threads[N_THREADS];  // array of pointers to the thread
char thread_names[N_THREADS][MAX_NAME];  // array of charater string for thread names

Semaphore *barrier;  // semaphores for barrier
Semaphore *mutex;    // semaphore for the mutual exclusion
int nCount = 0;      // number of waiting threads


void MakeTicks(int n)  // advance n ticks of simulated time
{
    int i;
    IntStatus oldLevel;

    oldLevel = interrupt->SetLevel(IntOff);
    for(i = 0; i < n; i++) {
        interrupt->SetLevel(IntOff);
        interrupt->SetLevel(IntOn);
    }
    interrupt->SetLevel(oldLevel);
}


void BarThread(_int which)
{
    MakeTicks(N_TICKS);
    printf("Thread %d rendezvous\n", which);

    mutex->P();
    nCount++;
    if(nCount == N_THREADS) {  // the last thread
        mutex->V();
        printf("Thread %d is the last\n", which);
        barrier->V();  // unblock ONE thread
    }
    else {  // not the last thread
        mutex->V();
        barrier->P();
        barrier->V();  // once we are unblocked, unblock the next thread
    }

    printf("Thread %d critical point\n", which);
}


//----------------------------------------------------------------------
// ThreadsBarrier
// 	Set up semaphores for n threads barrier problem
//	Create and fork threads
//----------------------------------------------------------------------

void ThreadsBarrier()
{
    int i;
    DEBUG('t', "ThreadsBarrier");

    // Semaphores
    barrier = new Semaphore("barrier", 0);
    mutex = new Semaphore("mutex", 1);

    // Create and fork N_THREADS threads 
    for(i = 0; i < N_THREADS; i++) {
        // this statemet is to form a string to be used as the name for thread i. 
        sprintf(thread_names[i], "thread_%d", i);

        // Create and fork a new thread using the name in thread_names[i] and 
        // integer i as the argument of function "BarThread"
        threads[i] = new Thread(thread_names[i]);
        threads[i]->Fork(BarThread, i);
    };
}

