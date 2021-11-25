# ARM Trainer

This is a minimal monitor and and hardware stack for a small microcontroller trainer board, that was used to teach a Computer Organization class to students from ages 11-14.

Microprocessor trainers were a commonplace tool used to learn computing in the 1970s and early 1980s.  They replicated the console found on minicomputers, and allowed engineers to gain experience writing programs in machine code.

The Computer Organization class that I taught was a "how computers work" class with minimal use of conventional computers in the classroom.  We focused initially on gates, building from there to understand simple combinatorial and sequential circuits, and eventually ALUs, register files, and microprocessor subsystems.  This culminated in a study of ARM-Thumb machine code.

ARM-Thumb machine code was a fairly ideal architecture for students to learn, because:

* It is a real architecture, in real use.
* It has a reasonable number of general purpose registers.
* A fixed-length, relatively simple encoding avoids unnecessary complexity.

