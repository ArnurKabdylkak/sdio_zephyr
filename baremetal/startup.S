/**
 * RISC-V Bare-metal Startup Code
 * LiteX compatible
 */

.section .text.start
.global _start
.type _start, @function

_start:
    /* Disable interrupts */
    csrci   mstatus, 8
    csrw    mie, zero

    /* Set global pointer */
.option push
.option norelax
    la      gp, __global_pointer$
.option pop

    /* Set stack pointer */
    la      sp, _fstack

    /* Copy .data section from ROM to RAM */
    la      t0, _fdata_rom      /* Source (ROM) */
    la      t1, _fdata          /* Destination (RAM) */
    la      t2, _edata          /* End of .data */
1:
    bge     t1, t2, 2f
    lw      t3, 0(t0)
    sw      t3, 0(t1)
    addi    t0, t0, 4
    addi    t1, t1, 4
    j       1b
2:

    /* Clear BSS section */
    la      t0, _fbss
    la      t1, _ebss
3:
    bge     t0, t1, 4f
    sw      zero, 0(t0)
    addi    t0, t0, 4
    j       3b
4:

    /* Call main */
    call    main

    /* If main returns, loop forever */
5:
    wfi
    j       5b

.size _start, . - _start
