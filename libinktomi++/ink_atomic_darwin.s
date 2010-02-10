#  Licensed to the Apache Software Foundation (ASF) under one
#  or more contributor license agreements.  See the NOTICE file
#  distributed with this work for additional information
#  regarding copyright ownership.  The ASF licenses this file
#  to you under the Apache License, Version 2.0 (the
#  "License"); you may not use this file except in compliance
#  with the License.  You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
#  Unless required by applicable law or agreed to in writing, software
#  distributed under the License is distributed on an "AS IS" BASIS,
#  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#  See the License for the specific language governing permissions and
#  limitations under the License.
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
