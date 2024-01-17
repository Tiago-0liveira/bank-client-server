#include "servidor.h"


int main() {
	
}


// QUEUE FUNCTIONS
Queue *makeQueue() {
	Queue *queue = (Queue *) malloc(sizeof(Queue));
	if (!queue)
		perror("Erro ao alocar memória para a fila.");
	initializeQueue(queue);
	return queue;
}
void	initializeQueue(Queue *queue)
{
	queue->front = -1;
	queue->rear = -1;
}
bool isEmpty(Queue *queue) {
	return queue->front == NULL;
}
bool isFull(Queue *queue) {
	return queue->rear == QUEUE_MAX_SIZE - 1;
}
void enqueue(Queue *queue, Pedido *pedido) {
    if (isFull(queue)) {
        printf("A fila está cheia. Não é possível adicionar mais elementos.\n");
    } else {
        if (isEmpty(queue)) {
            queue->front = 0;
        }
        queue->rear++;
        queue->array[queue->rear] = pedido;
    }
}
Pedido *dequeue(Queue *queue) {

    Pedido *data = NULL;

    if (isEmpty(queue)) {
        printf("A fila está vazia. Não há elementos para desenfileirar.\n");
    } else {
        data = queue->array[queue->front];
        if (queue->front == queue->rear) {
            initializeQueue(queue);
        } else {
            queue->front++;
        }
        printf("Elemento %d desenfileirado com sucesso.\n", data);
    }

    return data;
}
// QUEUE END