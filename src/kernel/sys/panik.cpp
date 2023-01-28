#include <sys/panik.h>
#include <sys/printk.h>
#include <cdefs.h>

/* Assume, as is often the case, that EBP is the first thing pushed. If not, we are in trouble. */
struct stackframe {
  struct stackframe* rbp;
  uint64_t rip;
};

static void UnwindStack(int MaxFrames) {
    struct stackframe *stk;
    stk = (stackframe*)__builtin_frame_address(0);
    //asm volatile ("mov %%rbp,%0" : "=r"(stk) ::);
    printk("Stack trace:\n");
    for(unsigned int frame = 0; stk && frame < MaxFrames; ++frame)
    {
        // Unwind to previous stack frame
        printk("  0x%x     \n", stk->rip);
        stk = stk->rbp;
    }
}

void panik(const char *message, const char *file, const char *function, unsigned int line) {
        asm volatile ("cli"); // We don't want interrupts while we are panicking

        #ifdef KCONSOLE_GOP
        GlobalRenderer.print_set_color(0xff0000ff, 0x00000000);
        #endif

        // Printing the panic message
        printk("\n\n!! PANIK!! \n");
        printk("Irrecoverable error in the kernel.\n\n");
        printk("%s version %s, build %s %s\n", KNAME, KVER, __DATE__, __TIME__);
        printk("%s in function %s at line %d\n", file, function, line);
        printk("Cause: %s\n", message);
	UnwindStack(5);
        printk("[Hanging now...]\n");

        while (true) {
                // Halting forever
                asm volatile ("hlt");
        }

}
