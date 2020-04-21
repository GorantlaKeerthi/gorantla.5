To compile we used the  make command
  $ make 
 we will get like this.
gcc -Wall -g -c proc.c
gcc -Wall -g -c clock.c
gcc -Wall -g -c shm.c
gcc -Wall -g oss.c proc.o clock.o shm.o -o oss -lrt -pthread
gcc -Wall -g user.c proc.o clock.o shm.o -o user -lrt -pthread

After compilation we can find oss file which will be green color.
2. Execute
To execute the program
use command ./oss
we will get output in output.txt file
policy to kill processes:
We will kill the first process found in deadlock.


Version Control:
I pushed all these files in to  github.
/classes/OS/gorantla/gorantla.5/log
