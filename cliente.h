#ifndef CLIENTE_H
#define CLIENTE_H

#include "common.h"
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <pthread.h>
#include <stdbool.h>
#include <errno.h>

typedef struct {
	int 		fifo_c2s;
	int 		fifo_s2c;
	pthread_t	answer_thread;
	bool		running;
	pthread_mutex_t mutex;
} ClientManager;

void	load_fifo_channels(ClientManager *ClientManager);
void	close_fifo_channels(ClientManager *ClientManager);
void	*answer_thread(void *arg);
void	wait_for_input(ClientManager *ClientManager);
Pedido	*make_pedido(ClientManager *ClientManager, char *msg);
void	send_pedido(ClientManager *ClientManager, Pedido *pedido);
bool	validar_pedido(Pedido *pedido);

#endif