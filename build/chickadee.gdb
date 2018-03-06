set $loaded = 1
set arch i386:x86-64
file obj/kernel.full
add-symbol-file obj/bootsector.full 0x7c00
add-symbol-file obj/p-allocator.full 0x100000
target remote localhost:12949
source build/functions.gdb
display/5i $pc
