#ifndef SERVIDOR_H
#define SERVIDOR_H

#include "common.h"
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdbool.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <sys/time.h>
#include <signal.h>
#include <errno.h>

#define SHM_NAME "banco_2xxxxxxx.shm"
#define SALDO_INICIAL 250.0
#define MINUTE_ISECS 60
#define MAX_RUNNING_PEDIDOS 30
#define SECOND_IMILLIS 1000
#define CRITIC_WAIT_TIME_IMILLIS SECOND_IMILLIS * 0.5
#define TWO_MIN_IMILLIS 2 * MINUTE_ISECS * SECOND_IMILLIS

typedef struct {
	int 	id;
	double	saldo;
} ContaBancaria;

typedef struct {
	ContaBancaria	contas[NUM_ACCOUNTS];
	bool			aberto;
} Banco;

typedef struct {
	pthread_mutex_t mutex_reader;
	pthread_mutex_t mutex_writer;
	pthread_cond_t 	cond_writer;
	pthread_mutex_t mutex_reader_count;
	int 			reader_count;
} BancoManager;

typedef struct {
	int 			shmid;
	Banco 			*banco;
	BancoManager	bancoManager;
} SharedMemory;

typedef struct ServerManager ServerManager;

typedef struct {
	Pedido 					*pedido;
	pthread_t 				thread;
	ServerManager			*ServerManager;
	bool					thread_running;
	bool					has_ran;
} PedidoThreaded;

struct ServerManager {
	int 			fifo_c2s;
	int 			fifo_s2c;
	SharedMemory	sharedMemory;
	PedidoThreaded	pedidos[MAX_RUNNING_PEDIDOS];
	pthread_mutex_t mutex;
	pthread_t		timer_thread;
	int				timer;
	bool 			running; // will be set to false when SIGINT|SIGALRM is received
};

void			open_fifo_channels(ServerManager *ServerManager);
void			close_fifo_channels(ServerManager *ServerManager);
bool			make_shared_memory(ServerManager *ServerManager);
void			unmap_shared_memory(ServerManager *ServerManager);
void			init_banco(Banco *banco, ServerManager *ServerManager);
void			init_ServerManager(ServerManager *ServerManager);
PedidoThreaded *get_first_free_pedido(ServerManager *ServerManager, Pedido *pedido);
void			wait_for_pedidos(ServerManager *ServerManager);
void			banco_signal_handler(int signal, siginfo_t *info, void *context);
bool			init_bancoManager(BancoManager *bancoManager);
void			*run_pedido(void *arg);
double			get_saldo(SharedMemory *SharedMemory, int numero_conta);
double			deposito(SharedMemory *SharedMemory, int numero_conta, double montante);		
double			transferencia(SharedMemory *SharedMemory, int numero_conta, int conta_destino, double montante);
double  		levantamento(SharedMemory *SharedMemory, int numero_conta, double montante);
void			fechar_banco(ServerManager *ServerManager);
void			escreve_resposta(ServerManager *ServerManager, Resposta *resposta);
void			wait_for_all_threads(ServerManager *ServerManager);
void			*timer_thread(void *arg);
int				get_time(void);
void			show_banco(SharedMemory *SharedMemory);
void			start_timer(ServerManager *ServerManager);
ServerManager	*get_serverManager(void);

#endif