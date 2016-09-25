# kmemspy

kmemspy is a Linux debugging tool which allows privileged users to inspect system physical memory and the virtual memory of any process. It consists of a kernel module and a user mode frontend.

## Installation

**Warning:** Loading the kernel module opens a significant security hole in the running system. The device node is accessible by root only but it provides enhanced memory access beyond the kernel's normal interface. The module should only be loaded during debugging sessions.

kmemspy requires a 4.6 or newer running kernel.

First, build the kernel module:

    cd kernel ; make ; cd ..

Next, build the user mode tool:

    make -C user

The kernel module must be loaded before using the user mode tool:

    sudo insmod kernel/kmemspy.ko

## Running

To inspect the virtual memory of a process, e.g.:

    $ sudo user/kmemspy -p `pidof a.out` 0x18d4010 40
    PTE: 0x800000101fc01025
    0x18d4010:       0x00000000      0x00000001      0x00000002      0x00000003
    0x18d4020:       0x00000004      0x00000005      0x00000006      0x00000007
    0x18d4030:       0x00000008      0x00000009      0x0000000a      0x0000000b
    0x18d4040:       0x0000000c      0x0000000d      0x0000000e      0x0000000f

To inspect a physical address, e.g.

    $ sudo user/kmemspy --phys 0x101fc01010 40
    0x101fc01010:       0x00000000      0x00000001      0x00000002      0x00000003
    0x101fc01020:       0x00000004      0x00000005      0x00000006      0x00000007
    0x101fc01030:       0x00000008      0x00000009      0x0000000a      0x0000000b
    0x101fc01040:       0x0000000c      0x0000000d      0x0000000e      0x0000000f
