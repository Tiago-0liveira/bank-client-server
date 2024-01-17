#ifndef SERVIDOR_H
#define SERVIDOR_H

#include "common.h"
#include "stdlib.h"
#include "stdio.h"
#include "stdbool.h"

#define QUEUE_MAX_SIZE 50

typedef struct {
	Pedido *array[QUEUE_MAX_SIZE];
    int front, rear;
} Queue;




// QUEUE FUNCTIONS

Queue 	*makeQueue();
void	initializeQueue(Queue *queue);
bool 	isEmpty(Queue *queue);
bool 	isFull(Queue *queue);
void 	enqueue(Queue *queue, Pedido *pedido);
Pedido *dequeue(Queue *queue);

#endif