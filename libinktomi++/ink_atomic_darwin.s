.text
	.align 4
.globl ink_atomic_swap	
.globl _ink_atomic_swap
.globl _ink_atomic_swap_ptr
_ink_atomic_swap_ptr:
ink_atomic_swap:
_ink_atomic_swap:
	lwarx r0, 0, r3
	stwcx. r4, 0, r3
	bne- _ink_atomic_swap
	mr r3,r0
	blr

	.align 4
.globl _ink_atomic_cas
.globl _ink_atomic_cas_ptr
_ink_atomic_cas_ptr:
_ink_atomic_cas:
	lwarx r0, 0, r3
	cmpw r4, r0
	bne- L3
	stwcx. r5, 0, r3
	bne- L3
	li r3, 1
	b L4
L3:	
	li r3, 0
L4:	
        blr
