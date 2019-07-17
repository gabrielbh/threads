#include <vector>
#include <signal.h>
#include <cstdlib>
#include <setjmp.h>
#include <iostream>
#include <bits/sigset.h>
#include <sys/time.h>
#include "uthreads.h"
#include "Thread.h"
#include <stack>


int SUCCESS = 0;
int FAILURE = -1;
int READY = 0;
int BLOCKED = 1;
int RUNNING = 2;
int TERMINATE = 4;
//int counter = 0;
int ids[MAX_THREAD_NUM]  = {};
sigjmp_buf env[MAX_THREAD_NUM];
#define STACK_SIZE 4096
typedef unsigned long address_t;
char stack[STACK_SIZE];
sigset_t signal_set{};

using namespace std;

#ifdef __x86_64__
/* code for 64 bit Intel arch */

typedef unsigned long address_t;
#define JB_SP 6
#define JB_PC 7

/* A translation is required when using an address of a variable.
   Use this as a black box in your code. */
address_t translate_address(address_t addr)
{
    address_t ret;
    asm volatile("xor    %%fs:0x30,%0\n"
            "rol    $0x11,%0\n"
    : "=g" (ret)
    : "0" (addr));
    return ret;
}

#else
/* code for 32 bit Intel arch */

typedef unsigned int address_t;
#define JB_SP 4
#define JB_PC 5

/* A translation is required when using an address of a variable.
   Use this as a black box in your code. */
address_t translate_address(address_t addr)
{
    address_t ret;
    asm volatile("xor    %%gs:0x18,%0\n"
		"rol    $0x9,%0\n"
                 : "=g" (ret)
                 : "0" (addr));
    return ret;
}

#endif
struct sigaction sa;
struct itimerval timer;

vector<Thread> readyThreadList = vector<Thread>();
vector<Thread> blockedThreads = vector<Thread>();

Thread* runningThread;
int total_quantums = 0;

//Block signals at the start of function
void startSignals()
{
    sigemptyset(&signal_set);
    sigaddset(&signal_set, SIGVTALRM);
    sigprocmask(SIG_BLOCK, &signal_set, nullptr);
}

//Unblock signals at the end of function
void endSignals()
{
    sigaddset(&signal_set, SIGVTALRM);
    sigprocmask(SIG_UNBLOCK, &signal_set, NULL);
}

//Reset the clock timer
void resetTimer()
{
    if (setitimer (ITIMER_VIRTUAL, &timer, NULL))
    {
        cerr<<"system error : reset timer error"<<endl;
        exit(300);
    }
}

//terminate a thread from a given vector
int terminateFromList(int tid, vector<Thread> &lst)
{
    for (int i = 0; i < lst.size(); i++)
    {
        if (lst[i].get_id() == tid)
        {
            for (int j = 0; j < MAX_THREAD_NUM; j++)
            {
                if(lst[i].get_syncs()[j])
                {

                    uthread_resume(i);
                }
            }
            ids[tid] = 0;
            lst.erase(lst.begin() + i);
            return SUCCESS;
        }
    }
    return FAILURE;
}

// When trying to make an action on the running thread or when rime expires we switch to the next thread in the queue
void switchThreads(int caller)
{

    startSignals();
    resetTimer();
    total_quantums++;
    Thread* nextThread = &readyThreadList[1];
    Thread* prevThread = &readyThreadList[0];
    runningThread = nextThread;
    prevThread->set_state(READY);
    if(readyThreadList.size() == 1)
    {
        runningThread->add_quantums();
        siglongjmp(env[readyThreadList[0].get_id()], 1);
    }
    else
    {
        int ret_val = sigsetjmp(env[prevThread->get_id()], 1);
        if (ret_val != 0)
        {
            resetTimer();
            endSignals();
            return;
        }

        else if (caller == TERMINATE)
        {
//            ids[readyThreadList[0].get_id()] = 0;
            terminateFromList(prevThread->get_id(),readyThreadList);
        }
        else if(caller == BLOCKED)
        {

            prevThread->set_state(BLOCKED);
            blockedThreads.push_back(*prevThread);
        }
        else if(caller == READY)
        {
            readyThreadList.push_back(*prevThread);

        }
        runningThread->set_state(RUNNING);
        runningThread->add_quantums();
        if (caller != TERMINATE)
        {
            readyThreadList.erase((readyThreadList.begin()));
        }
        runningThread = &readyThreadList[0];
        siglongjmp(env[runningThread->get_id()], 1);
    }
}

// Determines what to do when time expires
void timer_handler(int sig)
{

    startSignals();
    switchThreads(READY);
    endSignals();
}

// The clock timer function
void clock_timer(int qsecs) {


    sa.sa_handler = &timer_handler;
    if (sigaction(SIGVTALRM, &sa,NULL) < 0)
    {
        cerr<<"system error : set timer error"<<endl;
        exit(1);
    }

    timer.it_value.tv_sec = qsecs / 1000000;
    timer.it_value.tv_usec = qsecs % 1000000;

    timer.it_interval.tv_sec = qsecs / 1000000;
    timer.it_interval.tv_usec = qsecs % 1000000;

    resetTimer();
}

// Find the smallest available id
int findNextId()
{
    for (int i = 1; i < MAX_THREAD_NUM; i++)
    {
        if (ids[i] == 0)
        {
            ids[i] = 1;
            return i;
        }
    }
    return FAILURE;
}

/*
 * Description: This function initializes the thread library.
 * You may assume that this function is called before any other thread library
 * function, and that it is called exactly once. The input to the function is
 * the length of a quantum in micro-seconds. It is an error to call this
 * function with non-positive quantum_usecs.
 * Return value: On success, return 0. On failure, return -1.
*/
int uthread_init(int quantum_usecs)
{

    startSignals();
    if (quantum_usecs <= 0)
    {
        cerr << "thread library error: quantum_usecs must be positive\n";
        return FAILURE;
    }
    clock_timer(quantum_usecs);
    uthread_spawn(nullptr);
    readyThreadList[0].set_state(RUNNING);
    runningThread = &readyThreadList[0];
    total_quantums = 1;
    endSignals();
    return SUCCESS;
}

int uthread_spawn(void (*f)())
{

    startSignals();
    int id;

    if(readyThreadList.size() >= MAX_THREAD_NUM)
    {
        endSignals();
        return FAILURE;
    }

    if(readyThreadList.empty())
    {
        id = 0;
        ids[0] = 1;
    }

    else
    {
        id = findNextId();
    }

    if(id == FAILURE)
    {
        endSignals();
        return FAILURE;
    }
    auto* t = new Thread(id, READY, f);
//    a++;
    readyThreadList.push_back(*t);
    runningThread = &readyThreadList[0];
    if(t->get_id() != 0)
    {
        address_t sp, pc;
        sp = (address_t)t->get_stack() + STACK_SIZE - sizeof(address_t);
        pc = (address_t)f;
        (env[t->get_id()]->__jmpbuf)[JB_SP] = translate_address(sp);
        (env[t->get_id()]->__jmpbuf)[JB_PC] = translate_address(pc);
        int check = sigsetjmp(env[t->get_id()], 1);
        if (check != 0)
        {
            resetTimer();
            endSignals();
            return SUCCESS;
        }
    }

    sigemptyset(&env[t->get_id()]->__saved_mask);
    endSignals();
    return t->get_id();
}

// Delete all the items in the event of sudden exit
void releaseMemory()
{
    readyThreadList.clear();
    blockedThreads.clear();
}


/*
 * Description: This function terminates the thread with ID tid and deletes
 * it from all relevant control structures. All the resources allocated by
 * the library for this thread should be released. If no thread with ID tid
 * exists it is considered an error. Terminating the main thread
 * (tid == 0) will result in the termination of the entire process using
 * exit(0) [after releasing the assigned library memory].
 * Return value: The function returns 0 if the thread was successfully
 * terminated and -1 otherwise. If a thread terminates itself or the main
 * thread is terminated, the function does not return.
*/
int uthread_terminate(int tid)
{

    startSignals();
    int ans;
    if (tid < 0 || tid >= MAX_THREAD_NUM || ids[tid] == 0)
    {
        cerr << "thread library error: no such tid.\n";

        endSignals();
        return FAILURE;
    }
    if (tid == 0)
    {
        releaseMemory();
        endSignals();
        exit(0);
    }
    if (tid == readyThreadList[0].get_id())
    {
        switchThreads(TERMINATE);
    }
    ans = terminateFromList(tid, readyThreadList);
    if(ans == FAILURE)
    {
        ans = terminateFromList(tid, blockedThreads);
    }
    if (ans == FAILURE)
    {
        endSignals();
        return FAILURE;
    }

    endSignals();
    return SUCCESS;
}

/*
 * Description: This function blocks the thread with ID tid. The thread may
 * be resumed later using uthread_resume. If no thread with ID tid exists it
 * is considered as an error. In addition, it is an error to try blocking the
 * main thread (tid == 0). If a thread blocks itself, a scheduling decision
 * should be made. Blocking a thread in BLOCKED state has no
 * effect and is not considered an error.
 * Return value: On success, return 0. On failure, return -1.
*/
int uthread_block(int tid)
{

    startSignals();
    if (tid < 0 || tid > MAX_THREAD_NUM || ids[tid] == 0)
    {
        cerr << "thread library error: no such tid.\n";
        endSignals();
        return FAILURE;
    }
    if (tid == 0)
    {
        cerr << "thread library error: do not block the main thread.\n";
        endSignals();
        return FAILURE;
    }
    if (readyThreadList[0].get_id() == tid)
    {
        switchThreads(BLOCKED);
        return SUCCESS;

    }
    for (int i = 1; i < readyThreadList.size(); i++)
    {
        if (readyThreadList[i].get_id() == tid)
        {
            readyThreadList[i].set_state(BLOCKED);
            blockedThreads.push_back(readyThreadList[i]);
            readyThreadList.erase(readyThreadList.begin() + i);
            *runningThread = readyThreadList[0];
            endSignals();
            return SUCCESS;
        }
    }
    endSignals();
    return FAILURE;
}

/*
 * Description: This function resumes a blocked thread with ID tid and moves
 * it to the READY state if it's not synced. Resuming a thread in a RUNNING or READY state
 * has no effect and is not considered as an error. If no thread with
 * ID tid exists it is considered an error.
 * Return value: On success, return 0. On failure, return -1.
*/
int uthread_resume(int tid)
{

    startSignals();
    if (tid <= 0 || tid > MAX_THREAD_NUM || ids[tid] == 0)
    {
        cerr << "thread library error: no such tid is blocked.\n";
        endSignals();
        return FAILURE;
    }
    for (int i = 0; i < blockedThreads.size(); i++)
    {
        if(blockedThreads[i].get_id()== tid)
        {
            blockedThreads[i].set_state(READY);
            readyThreadList.push_back(blockedThreads[i]);
            blockedThreads.erase(blockedThreads.begin() + i);
            endSignals();
            return SUCCESS;
        }
    }
    endSignals();
    return SUCCESS;
}

/*
 * Description: This function blocks the RUNNING thread until thread with
 * ID tid will terminate. It is considered an error if no thread with ID tid
 * exists, if thread tid calls this function or if the main thread (tid==0) calls this function.
 * Immediately after the RUNNING thread transitions to the BLOCKED state a scheduling decision
 * should be made.
 * Return value: On success, return 0. On failure, return -1.
*/
int uthread_sync(int tid)
{

    startSignals();
    if (tid < 0 || tid > MAX_THREAD_NUM || ids[tid] == 0)
    {
        cerr << "thread library error: no such tid.\n";
        endSignals();
        return FAILURE;
    }
    if (tid == runningThread->get_id())
    {
        cerr << "thread library error: running thread can not call this function.\n";
        endSignals();
        return FAILURE;
    }
    if (tid == 0)
    {
        cerr << "thread library error: main thread can not call this function.\n";
        endSignals();
        return FAILURE;
    }
    if(tid == readyThreadList[0].get_id())
    {
        endSignals();
        return FAILURE;
    }
    Thread* temp = &readyThreadList[0];
    uthread_block(readyThreadList[0].get_id());
    for (int i = 0; i < readyThreadList.size(); ++i) {
        if (readyThreadList[i].get_id() == tid)
        {
            readyThreadList[i].set_syncs(temp->get_id());
            break;
        }
    }

    endSignals();

}

/*
 * Description: This function returns the thread ID of the calling thread.
 * Return value: The ID of the calling thread.
*/
int uthread_get_tid()
{
    return runningThread->get_id();
}

/*
 * Description: This function returns the total number of quantums since
 * the library was initialized, including the current quantum.
 * Right after the call to uthread_init, the value should be 1.
 * Each time a new quantum starts, regardless of the reason, this number
 * should be increased by 1.
 * Return value: The total number of quantums.
*/
int uthread_get_total_quantums()
{
    return total_quantums;
}

/*
 * Description: This function returns the number of quantums the thread with
 * ID tid was in RUNNING state. On the first time a thread runs, the function
 * should return 1. Every additional quantum that the thread starts should
 * increase this value by 1 (so if the thread with ID tid is in RUNNING state
 * when this function is called, include also the current quantum). If no
 * thread with ID tid exists it is considered an error.
 * Return value: On success, return the number of quantums of the thread with ID tid.
 * 			     On failure, return -1.
*/
int uthread_get_quantums(int tid)
{


    startSignals();

    if (tid < 0 || tid > MAX_THREAD_NUM || ids[tid] == 0)
    {
        cerr << "thread library error: no such tid.\n";
        endSignals();
        return FAILURE;
    }

    for (Thread t: readyThreadList)
    {
        if(t.get_id() == tid)
        {
            endSignals();
            return t.get_quantums();
        }
    }

    for (Thread t: blockedThreads)
    {
        if(t.get_id() == tid)
        {
            endSignals();
            return t.get_quantums();
        }
    }
    endSignals();
    return FAILURE;
}
