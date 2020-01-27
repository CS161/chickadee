#include "u-lib.hh"
#define ALLOC_SLOWDOWN 24

extern uint8_t end[];

uint8_t* heap_top;
uint8_t* stack_bottom;

void process_main() {
    sys_kdisplay(KDISPLAY_MEMVIEWER);

    pid_t p = sys_getpid();
    srand(p);

    heap_top = reinterpret_cast<uint8_t*>(
        round_up(reinterpret_cast<uintptr_t>(end), PAGESIZE)
    );
    stack_bottom = reinterpret_cast<uint8_t*>(
        round_down(rdrsp() - 1, PAGESIZE)
    );

    while (true) {
        // Add code to this loop to call your new, nasty system call
        // with some probability!

        if (rand(0, ALLOC_SLOWDOWN - 1) < p) {
            if (heap_top == stack_bottom || sys_page_alloc(heap_top) < 0) {
                break;
            }
            *heap_top = p;      /* check we have write access to new page */
            heap_top += PAGESIZE;
        }
        sys_yield();
        if (rand() < RAND_MAX / 32) {
            sys_pause();
        }
    }

    while (true) {
        sys_yield();
    }
}
