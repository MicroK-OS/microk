#include <sys/panik.h>
#include <sys/printk.h>
#include <stdlib.h>
#include <cdefs.h>

__attribute__((noreturn))
void panik(const char *message, const char *file, const char *function, unsigned int line) {
        asm volatile ("cli"); // We don't want interrupts while we are panicking

        //GlobalRenderer.print_set_color(0xff0000ff, 0x00000000);

        // Printing the panic message
        printk("\n\n!! PANIK!! \n");
        printk("Irrecoverable error in the kernel.\n\n");
        printk("%s version %s, build %s %s\n", CONFIG_KERNEL_CNAME, CONFIG_KERNEL_CVER, __DATE__, __TIME__);
        printk("%s in function %s at line %d\n", file, function, line);
        printk("Cause: %s\n", message);
	UnwindStack(5);
        printk("[Hanging now...]\n");

        while (true) {
                // Halting forever
                asm volatile ("hlt");
        }

}