Here is a full Oops for reference
```
Unable to handle kernel NULL pointer dereference at virtual address 0000000000000000
Mem abort info:
  ESR = 0x96000045
  EC = 0x25: DABT (current EL), IL = 32 bits
  SET = 0, FnV = 0
  EA = 0, S1PTW = 0
  FSC = 0x05: level 1 translation fault
Data abort info:
  ISV = 0, ISS = 0x00000045
  CM = 0, WnR = 1
user pgtable: 4k pages, 39-bit VAs, pgdp=00000000424ed000
[0000000000000000] pgd=0000000000000000, p4d=0000000000000000, pud=0000000000000000
Internal error: Oops: 96000045 [#1] SMP
Modules linked in: hello(O) faulty(O) scull(O)
CPU: 0 PID: 159 Comm: sh Tainted: G           O      5.15.18 #1
Hardware name: linux,dummy-virt (DT)
pstate: 80000005 (Nzcv daif -PAN -UAO -TCO -DIT -SSBS BTYPE=--)
pc : faulty_write+0x14/0x20 [faulty]
lr : vfs_write+0xa8/0x2b0
sp : ffffffc008d23d80
x29: ffffffc008d23d80 x28: ffffff80024fb300 x27: 0000000000000000
x26: 0000000000000000 x25: 0000000000000000 x24: 0000000000000000
x23: 0000000040001000 x22: 0000000000000012 x21: 0000005587f32670
x20: 0000005587f32670 x19: ffffff8002087800 x18: 0000000000000000
x17: 0000000000000000 x16: 0000000000000000 x15: 0000000000000000
x14: 0000000000000000 x13: 0000000000000000 x12: 0000000000000000
x11: 0000000000000000 x10: 0000000000000000 x9 : 0000000000000000
x8 : 0000000000000000 x7 : 0000000000000000 x6 : 0000000000000000
x5 : 0000000000000001 x4 : ffffffc0006f7000 x3 : ffffffc008d23df0
x2 : 0000000000000012 x1 : 0000000000000000 x0 : 0000000000000000
Call trace:
 faulty_write+0x14/0x20 [faulty]
 ksys_write+0x68/0x100
 __arm64_sys_write+0x20/0x30
 invoke_syscall+0x54/0x130
 el0_svc_common.constprop.0+0x44/0xf0
 do_el0_svc+0x40/0xa0
 el0_svc+0x20/0x60
 el0t_64_sync_handler+0xe8/0xf0
 el0t_64_sync+0x1a0/0x1a4
Code: d2800001 d2800000 d503233f d50323bf (b900003f) 
---[ end trace 243f05b8c04c717b ]---
```
Details
```
`# lsmod`
The output confirms `faulty` dfriver was indeed loaded
```
Module                  Size  Used by    Tainted: G  
hello                  16384  0 
faulty                 16384  0 
scull                  24576  0 
```
This cvommand results in kernel panic
```
`# echo "hello_world" > /dev/faulty`
```
```
Unable to handle kernel NULL pointer dereference at virtual address 0000000000000000
```
An attempt was made to dereference a NULL pointer. The register names suggest AArm64 architecture
```
Mem abort info:
  ESR = 0x96000045
  EC = 0x25: DABT (current EL), IL = 32 bits
  SET = 0, FnV = 0
  EA = 0, S1PTW = 0
  FSC = 0x05: level 1 translation fault
Data abort info:
  ISV = 0, ISS = 0x00000045
  CM = 0, WnR = 1
```
According to [this info](https://esr.arm64.dev/#0x96000045) this means
* 32-bit instruction trapped
* Abort caused by writing to memory
* Translation fault, level 1.
More details are [here](https://github.com/google/aarch64-esr-decoder)
```
user pgtable: 4k pages, 39-bit VAs, pgdp=00000000424ed000
[0000000000000000] pgd=0000000000000000, p4d=0000000000000000, pud=0000000000000000
```
This informs which page table was affected, it's NULL page
```
Internal error: Oops: 96000045 [#1] SMP
```
[#1] means number of thimes Oops occured
```
Modules linked in: hello(O) faulty(O) scull(O)
CPU: 0 PID: 159 Comm: sh Tainted: G           O      5.15.18 #1
```
The kernel is tainted because	
G	proprietary module was loaded
O	externally-built ("out-of-tree") module was loaded
according to [kernel documentation](https://www.kernel.org/doc/html/latest/admin-guide/tainted-kernels.html)
```
Hardware name: linux,dummy-virt (DT)
pstate: 80000005 (Nzcv daif -PAN -UAO -TCO -DIT -SSBS BTYPE=--)
```
the function where the Oops happened
```
pc : faulty_write+0x14/0x20 [faulty]
```
Return from the call goes there
```
lr : vfs_write+0xa8/0x2b0
```
Register dump (not really useful in this case because by now it is obvious the problem was cvaused by a write to 0 page)
```
sp : ffffffc008d23d80
x29: ffffffc008d23d80 x28: ffffff80024fb300 x27: 0000000000000000
x26: 0000000000000000 x25: 0000000000000000 x24: 0000000000000000
x23: 0000000040001000 x22: 0000000000000012 x21: 0000005587f32670
x20: 0000005587f32670 x19: ffffff8002087800 x18: 0000000000000000
x17: 0000000000000000 x16: 0000000000000000 x15: 0000000000000000
x14: 0000000000000000 x13: 0000000000000000 x12: 0000000000000000
x11: 0000000000000000 x10: 0000000000000000 x9 : 0000000000000000
x8 : 0000000000000000 x7 : 0000000000000000 x6 : 0000000000000000
x5 : 0000000000000001 x4 : ffffffc0006f7000 x3 : ffffffc008d23df0
x2 : 0000000000000012 x1 : 0000000000000000 x0 : 0000000000000000
```
The call trace helps find the location of the problem
```
Call trace:
 faulty_write+0x14/0x20 [faulty]
 ksys_write+0x68/0x100
 __arm64_sys_write+0x20/0x30
 invoke_syscall+0x54/0x130
 el0_svc_common.constprop.0+0x44/0xf0
 do_el0_svc+0x40/0xa0
 el0_svc+0x20/0x60
 el0t_64_sync_handler+0xe8/0xf0
 el0t_64_sync+0x1a0/0x1a4
```
The bug is in the function faulty_write, module faulty
The function was invoked by ksys_write, wgich in turn was called by write syscall, and so on ...
Thebug in the function faulty_write happens at location 0x14, theunction itself is 0x20 bytes long. This information makes iot easy to find the place of the bug.
```
ssize_t faulty_write (struct file *filp, const char __user *buf, size_t count,
		loff_t *pos)
{
	/* make a simple fault by dereferencing a NULL pointer */
	*(int *)0 = 0;   // FIXME
	return 0;
}
```
A hex-dump of the section of machine code that was being run at the time the Oops occurred.
```
Code: d2800001 d2800000 d503233f d50323bf (b900003f) 
---[ end trace 243f05b8c04c717b ]---
```
Analysis made based on [this info](https://www.opensourceforu.com/2011/01/understanding-a-kernel-oops/)

