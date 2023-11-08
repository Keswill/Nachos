#include "copyright.h"
#include "system.h"



void
SimpleThread(int which)
{
    int num;
    
    for (num = 0; num < 5; num++) {
        printf("*** thread %d looped %d times, priority=%d\n", (int) which, num, currentThread->getPriority());
        if(num % 2 == 0 && num != 4)
            currentThread->Yield();
    }
}



void
ThreadTest()
{
    DEBUG('t', "Entering SimpleTest");

    Thread *t1 = new Thread("forked thread 1");
    t1->setPriority(1);
    Thread *t2 = new Thread("forked thread 2");
    t2->setPriority(2);
    Thread *t3 = new Thread("forked thread 3");
    t3->setPriority(3);

    t1->Fork(SimpleThread, 1);
    t2->Fork(SimpleThread, 2);
    t3->Fork(SimpleThread, 3);
    SimpleThread(0);
}

