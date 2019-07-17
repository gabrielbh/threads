#include "Thread.h"


Thread::Thread() = default;

Thread::Thread(int id, int state, void (*f)(void))
{
    this->id = id;
    this->state = state;
    this->f = f;
    char stack[4096];
    this->quantums = 0;
}

int Thread::get_id()
{
    return id;
}

char *Thread::get_stack() {
    return stack;
}

bool *Thread::get_syncs() {
    return syncs;
}

void Thread::set_state(int state) {
    this->state = state;
}

int Thread::get_quantums() {
    return this->quantums;
}

void Thread::add_quantums() {
    this->quantums++;
}

void Thread::set_syncs(int id) {
    syncs[id] = true;
}