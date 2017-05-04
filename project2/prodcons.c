/*
 * James Hahn
 * CS1550 Project 2
 * Tu/Th 2:30-3:45pm
 * Recitation: Fr 12-1pm
 *
 * This project is to show a solution to the producer-consumer problem
 * with counting semaphores.  The program takes 3 command-line arguments:
 * 	1) The number of consumers
 * 	2) The number of producers
 * 	3) The size of buffer to use
 * in that order.  Then, the program produces sequential integers, which
 * are consumed.  The program runs without deadlock and in an infinite loop.
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>

#define TRUE 1
#define __NR_cs1550_down 325 //down() is syscall 325
#define __NR_cs1550_up 326 //up() is syscall 326

struct cs1550_sem{
	int value;
	struct node* head; //Process queue -- Linked list is 0(1) removal and add with head and tail nodes
	struct node* tail; //Tail node for the process queue linked list
};

void up(struct cs1550_sem* semaphore){
	syscall(__NR_cs1550_up, semaphore);
}

void down(struct cs1550_sem* semaphore){
	syscall(__NR_cs1550_down, semaphore);
}

int main(int argc, char* argv[]){
	int producers = 0;
	int consumers = 0;
	int size_of_buffer = 0;

	if(argc != 4){ //Four arguments: executable (# of consumers) (# of producers) (size of buffer)
		printf("Illegal number of arguments; 3 is required!\n");
		return 1;
	} else{ //Parse the command-line arguments and make sure they're valid
		consumers = strtol(argv[1], NULL, 10);
		producers = strtol(argv[2], NULL, 10);
		size_of_buffer = strtol(argv[3], NULL, 10);

		if(consumers == 0 || producers == 0 || size_of_buffer == 0){
			printf("One of the arguments is the value 0; ILLEGAL.\n");
			return 1;
		}
	}
	
	//Reserved space for the semaphores to store their data, which is shared between the producers and consumers
	struct cs1550_sem* semaphore_memory = (struct cs1550_sem*) mmap(NULL, sizeof(struct cs1550_sem)*3, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, 0, 0);	

	//Shared variable data for the producers and consumers; includes the buffer, 'in' and 'out' counters (from Misurda's slides)
	//mmap() allows for inter-process communication (IPC) between the producers and consumers
	int* shared_memory = (int*) mmap(NULL, (size_of_buffer+2)*sizeof(int), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, 0, 0);

	//Later on, helps keep track of positions in the buffer for the producers and consumers respectively
	int curr_producers = 0;
	int curr_consumers = 0;

	//full semaphore; the number of resources used
	struct cs1550_sem* full;
	full = semaphore_memory + 1; //The 2nd semaphore mapped in memory
	full->value = 0; //0 resources used thus far
	full->head = NULL;
	full->tail = NULL;
	//empty semaphore; the number of resources available
	struct cs1550_sem* empty;
	empty = semaphore_memory; //The 1st semaphore mapped in memory
	empty->value = size_of_buffer; //We have N, or size_of_buffer, resources available
	full->head = NULL;
	full->tail = NULL;
	//mutex semaphore; a lock on the critical regions
	struct cs1550_sem* mutex;
	mutex = semaphore_memory + 2; //The 3nd semaphore mapped in memory
	mutex->value = 1; //Initially set to unlock
	mutex->head = NULL;
	mutex->tail = NULL;

	//Similar to the 'in' variable in Misurda's slides
	int* curr_produced = shared_memory;
	*curr_produced = 0;
	//Similar to the 'out' variable in Misurda's slides
	int* curr_consumed = shared_memory + 1;
	*curr_consumed = 0;
	//Beginning of the buffer shared between processes in memory
	int* buffer_ptr = shared_memory + 2;	

	//For the following two loops, fork() the parent until the necessary
	//producers and consumers are generated.  This one parent process
	//will generate all of the necessary child processes.  Then, it will
	//exit from the for loops and wait(), which allows the user to 
	//Ctrl+C out of the program; without the wait(), it would not be possible.

	int i;	

	for(i = 0; i < producers; i++){ //Create the producers
		if(fork() == 0){
			int item;
			while(TRUE){ //Nearly identical to Misurda's slides
				down(empty);
				down(mutex);
				item = *curr_produced;
				buffer_ptr[*curr_produced % size_of_buffer] = item; //Insert the item into the buffer; curr_produced increments forever, so make sure it doesn't escape the bounds of the buffer
				printf("Producer %c produced: %d\n", (i+65), item);
				*curr_produced += 1;
				up(mutex);
				up(full);
			}
		}
	}

	for(i = 0; i < consumers; i++){ //Create the consumers
		if(fork() == 0){
			int item;
			while(TRUE){ //Nearly identical to Misurda's slides
				down(full);
				down(mutex);	
				item = buffer_ptr[*curr_consumed % size_of_buffer]; //Grab the item from the buffer; curr_consumed increments forever, so make sure it doesn't escape the bounds of the buffer
				printf("Consumer %c consumed: %d\n", (i+65), item);
				*curr_consumed += 1;
				up(mutex);
				up(empty);
			}
		}
	}
	
	//Parent is sitting here as the children continue their IPC	
	int status;
	wait(&status);

	return 0;
}
