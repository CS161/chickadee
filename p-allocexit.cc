#include "u-lib.hh"
#ifndef ALLOC_SLOWDOWN
#define ALLOC_SLOWDOWN 18
#endif

extern uint8_t end[];

uint8_t* heap_top;
uint8_t* stack_bottom;

void process_main() {
    sys_kdisplay(KDISPLAY_MEMVIEWER);

    // First process never allocates; it alternates between forking children
    // and yielding; sometimes it exits. Each forked child allocates.
    while (true) {
        (void) sys_waitpid(0, nullptr, W_NOHANG);
        int x = rand(0, ALLOC_SLOWDOWN);
        if (x == 0) {
            // fork, then either exit or start allocating
            pid_t p = sys_fork();
            int choice = rand(0, 2);
            if (choice == 0 && p > 0) {
                sys_exit(0);
            } else if (choice != 2 ? p > 0 : p == 0) {
                break;
            }
        } else {
            sys_yield();
        }
    }

    int speed = rand(1, 16);

    // The heap starts on the page right after the 'end' symbol,
    // whose address is the first address not allocated to process code
    // or data.
    uint8_t* data_top = heap_top = reinterpret_cast<uint8_t*>(
        round_up(reinterpret_cast<uintptr_t>(end), PAGESIZE)
    );

    // The bottom of the stack is the first address on the current
    // stack page (this process never needs more than one stack page).
    stack_bottom = reinterpret_cast<uint8_t*>(
        round_down(rdrsp() - 1, PAGESIZE)
    );

    unsigned nalloc = 0;

    // Allocate heap pages until out of address space,
    // forking along the way.
    while (heap_top != stack_bottom) {
        if (rand(0, 6 * ALLOC_SLOWDOWN) >= 8 * speed) {
            (void) sys_waitpid(0, nullptr, W_NOHANG);
            sys_yield();
            continue;
        }

        int x = rand(0, 7 + min(nalloc / 4, 10U));
        if (x < 2) {
            if (sys_fork() == 0) {
                speed = rand(1, 16);
            }
        } else if (x < 3) {
            sys_exit(0);
        } else if (sys_page_alloc(heap_top) >= 0) {
            *heap_top = speed;      // check we have write access to new page
            heap_top += PAGESIZE;
            nalloc = (heap_top - data_top) / PAGESIZE;
        } else if (nalloc < 4) {
            sys_exit(0);
        } else {
            nalloc -= 4;
        }
    }

    // After running out of memory
    while (true) {
        if (rand(0, 2 * ALLOC_SLOWDOWN - 1) == 0) {
            sys_exit(0);
        } else {
            (void) sys_waitpid(0, nullptr, 0);
            sys_yield();
        }
    }
}
