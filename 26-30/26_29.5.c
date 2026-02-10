/******************************************************************************

Welcome to GDB Online.
GDB online is an online compiler and debugger tool for C, C++, Python, Java, PHP, Ruby, Perl,
C#, OCaml, VB, Swift, Pascal, Fortran, Haskell, Objective-C, Assembly, HTML, CSS, JS, SQLite, Prolog.
Code, Compile, Run and Debug online from anywhere in world.

*******************************************************************************/
#include <stdio.h>
#include <assert.h>
#include <pthread.h>

static volatile int counter = 0;

void *mythread (void *arg) {
    printf ("%s\n begin", (char *) arg);
    
    for(int i = 0 ; i < 1e7; i++) {
        counter = counter + 1;
    }
    
    printf("%s: done\n", (char *) arg);
    
    return NULL;
}

int main()
{
    pthread_t p1, p2;
    int rc;
    printf("main :begin\n");
    rc = pthread_create(&p1, NULL, mythread, "A");
    assert(rc == 0);
    rc = pthread_create(&p2, NULL, mythread, "B");
    assert(rc == 0);
    
    rc = pthread_join(p1, NULL); assert(rc == 0);
    rc = pthread_join(p2, NULL); assert(rc == 0);
    printf("main: end, counter = %d\n", counter);

    return 0;
}
