# ARM Trainer

This is a minimal monitor and and hardware stack for a small microcontroller trainer board, that was used to teach a Computer Organization class to students from ages 11-14.

Microprocessor trainers were a commonplace tool used to learn computing in the 1970s and early 1980s.  They replicated the console found on minicomputers, and allowed engineers to gain experience writing programs in machine code.

The Computer Organization class that I taught was a "how computers work" class with minimal use of conventional computers in the classroom.  We focused initially on gates, building from there to understand simple combinatorial and sequential circuits, and eventually ALUs, register files, and microprocessor subsystems.  This culminated in a study of ARM-Thumb machine code.

ARM-Thumb machine code was a fairly ideal architecture for students to learn, because:

* It is a real architecture, in real use.
* It has a reasonable number of general purpose registers.
* A fixed-length, relatively simple encoding avoids unnecessary complexity.

## Example Program

By a student, Robin K:

    20000000	DF20		(clear screen)
    20000002	2001		(mov r0, #1)
    20000004	2102		(mov r1, #1)
    20000006	180A		(add r2, r0, r1)
    20000008	DF21		(print contents of register 0)
    2000000A	4608		(mov r0, r1)
    2000000C	4611		(mov r1, r2)
    2000000E	E7FA		(b 20000006)

## TODO

* It would be nice to use SPI DMA to update the screen.  This would improve performance a little, and it would also mean we'd spend more time scanning the keyboard for input and not risk losing keystrokes.
* It would be nice to scan the keyboard smarter to detect interrupts usin$g less tie.  We could e.g. enable *all* output rows and then see if any lines change.
* Relative branches were difficult for students to compute.  Finding a better way to explain this, in both lessons and manuals, would be helpful.
* The manual should be updated with some of the system calls that have not been documented.
* Upload overall curriculum, etc, should get posted here.
