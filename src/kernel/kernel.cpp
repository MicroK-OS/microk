/*
 *  __  __  _                _  __        ___   ___
 * |  \/  |(_) __  _ _  ___ | |/ /       / _ \ / __|
 * | |\/| || |/ _|| '_|/ _ \|   <       | (_) |\__ \
 * |_|  |_||_|\__||_|  \___/|_|\_\       \___/ |___/
 *
 * MicroKernel, a simple futiristic Unix-based kernel
 * Copyright (C) 2022-2022 Mutta Filippo
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <stdlib.h>
#include <stdio.h>
#include <sys/printk.h>
#include <kutil.h>
#include <mm/heap.h>
#include <mm/pageframe.h>
#include <mm/memory.h>
#include <sys/cstr.h>
#include <dev/tty/tty.h>
#include <fs/vfs.h>
#include <sys/panik.h>
#include <proc/scheduler.h>
//#include <proc/thread.h>

#define PREFIX "[KINIT] "
/*
struct thread *current, *next;

void thread_function() {
	int thread_id = current->tid;

	while(true) {
		printk("Thread %d\n", thread_id);

		struct thread *_next = next;
		struct thread *_current = current;

		// Update "scheduler"
		next = _current;
		current = _next;

		// Switch thread
		switch_stack(&_current->stack_ptr, &_next->stack_ptr);
	}
}

current = new_thread(thread_function);
next = new_thread(thread_function);

uint64_t dummy_stack_ptr;
switch_stack(&dummy_stack_ptr, &current->stack_ptr);
*/

extern "C" void _start(BootInfo* bootInfo){
        kinit(bootInfo);

        GlobalRenderer.print_clear();

        printf(" __  __  _                _  __    ___   ___\n"
               "|  \\/  |(_) __  _ _  ___ | |/ /   / _ \\ / __|\n"
               "| |\\/| || |/ _|| '_|/ _ \\|   <   | (_) |\\__ \\\n"
               "|_|  |_||_|\\__||_|  \\___/|_|\\_\\   \\___/ |___/\n"
               "The operating system from the future...at your fingertips.\n"
               "\n"
               " Memory Status:\n"
               " -> Kernel:      %dkb.\n"
	       " -> Initrd:      %dkb.\n"
               " -> Free:        %dkb.\n"
               " -> Used:        %dkb.\n"
               " -> Reserved:    %dkb.\n"
               " -> Total:       %dkb.\n"
               "\n",
                kInfo.kernel_size / 1024,
		bootInfo->initrdSize / 1024,
                GlobalAllocator.GetFreeMem() / 1024,
                GlobalAllocator.GetUsedMem() / 1024,
                GlobalAllocator.GetReservedMem() / 1024,
                (GlobalAllocator.GetFreeMem() + GlobalAllocator.GetUsedMem()) / 1024);

	printf(" Active Filesystems:\n");
	VFS_Print(rootfs);
	VFS_Print(devtmpfs);
	VFS_Print(sysfs);

	printf("\n\nContinuing startup...\n");

	GlobalTTY->Activate();

	init_scheduler();
	start_scheduler();

        printf("Done!\n");

        while (true) {
                asm("hlt");
        }

        PANIK("Reached the end of kernel operations.");
}
