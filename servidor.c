#include "servidor.h"

int	main(void)
{
	ServerManager	ServerManager;

	open_fifo_channels(&ServerManager);
	init_banco(ServerManager.sharedMemory.banco, &ServerManager);
	alarm(MINUTE_ISECS * 2);
	wait_for_pedidos(&ServerManager);
	wait_for_all_threads(&ServerManager);
	close_fifo_channels(&ServerManager);
	unmap_shared_memory(&ServerManager);
}

bool	make_shared_memory(ServerManager *ServerManager)
{
	ServerManager->sharedMemory.shmid = shm_open(SHM_NAME, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
	if (ServerManager->sharedMemory.shmid == -1)
		return (perror("failed creating shared memory!"), false);
	if (ftruncate(ServerManager->sharedMemory.shmid, sizeof(Banco)) == -1)
        return (perror("Erro ao configurar o tamanho do objeto de memória compartilhada"), false);
	ServerManager->sharedMemory.banco = mmap(NULL, sizeof(Banco), PROT_READ | PROT_WRITE, MAP_SHARED, ServerManager->sharedMemory.shmid, 0);
    if (ServerManager->sharedMemory.banco == MAP_FAILED)
        return (perror("Erro ao mapear o objeto de memória compartilhada"), false);
	return (true);
}
void	unmap_shared_memory(ServerManager *ServerManager)
{
	if (munmap(ServerManager->sharedMemory.banco, sizeof(Banco)) == -1)
        return (perror("Erro ao desmapear o objeto de memória compartilhada"), NULL);
}
void	open_fifo_channels(ServerManager *ServerManager)
{
	if (mkfifo(CLIENT_TO_SERVER_FIFO, USER_LEVEL_PERMISSIONS) == -1)
		perror("failed mkfifo c2s");
	if (mkfifo(SERVER_TO_CLIENT_FIFO, USER_LEVEL_PERMISSIONS) == -1)
		perror("failed mkfifo s2c");
	ServerManager->fifo_c2s = open(CLIENT_TO_SERVER_FIFO, O_RDONLY);
	if (ServerManager->fifo_c2s == -1)
		perror("failed open c2s");
	ServerManager->fifo_s2c = open(SERVER_TO_CLIENT_FIFO, O_WRONLY);	
	if (ServerManager->fifo_s2c == -1)
		perror("failed open s2c");
}
void	close_fifo_channels(ServerManager *ServerManager)
{
	close(ServerManager->fifo_c2s);
	close(ServerManager->fifo_s2c);
	unlink(CLIENT_TO_SERVER_FIFO);
	unlink(SERVER_TO_CLIENT_FIFO);
}

void	*wait_for_pedidos(ServerManager *ServerManager)
{
	Pedido			pedidoIncoming;
	size_t			bytesRead;
	
	printf("Servidor iniciado!\n");
	pthread_mutex_lock(&ServerManager->mutex);
	do
	{
		pthread_mutex_unlock(&ServerManager->mutex);
		bytesRead = read(ServerManager->fifo_c2s, &pedidoIncoming, sizeof(Pedido));
		if (bytesRead > 0)
		{
			printf("Pedido: %d|%d|%f€|%d\n", pedidoIncoming.tipo, pedidoIncoming.numero_conta, pedidoIncoming.montante, pedidoIncoming.conta_destino);
			PedidoThreaded *pedidoThreaded = get_first_free_pedido(ServerManager, &pedidoIncoming);
			pedidoThreaded->ServerManager = ServerManager;
			pthread_create(&pedidoThreaded->thread, NULL, run_pedido, pedidoThreaded);
		}
		pthread_mutex_lock(&ServerManager->mutex);
	} while (bytesRead > 0 && ServerManager->running);
	pthread_mutex_unlock(&ServerManager->mutex);
}

void	wait_for_all_threads(ServerManager *ServerManager)
{
	int i = 0;
	for (i = 0; i < MAX_RUNNING_PEDIDOS; i++)
		if (ServerManager->pedidos[i].thread_running)
			pthread_join(ServerManager->pedidos[i].thread, NULL);

}

void	*run_pedido(void *arg)
{
	PedidoThreaded	*pedidoThreaded = (PedidoThreaded *)arg;
	Resposta		resposta;
	Banco			*banco = pedidoThreaded->ServerManager->sharedMemory.banco;

	resposta.pedido = pedidoThreaded->pedido;
	switch (pedidoThreaded->pedido.tipo)
	{
	case CONSULTA:
		resposta.status = STATUS_OK;
		resposta.saldo = get_saldo(pedidoThreaded->ServerManager->sharedMemory, pedidoThreaded->pedido.numero_conta);
		break;
	case DEPOSITO:
		if (!banco->aberto) {
			resposta.status = STATUS_FECHADO;
			resposta.saldo = get_saldo(pedidoThreaded->ServerManager->sharedMemory, pedidoThreaded->pedido.numero_conta);
			break;
		}
		resposta.status = STATUS_OK;
		resposta.saldo = deposito(banco, pedidoThreaded->pedido.numero_conta, pedidoThreaded->pedido.montante);
		break;
	case TRANSFERENCIA:
		if (!banco->aberto) {
			resposta.status = STATUS_FECHADO;
			resposta.saldo = get_saldo(pedidoThreaded->ServerManager->sharedMemory, pedidoThreaded->pedido.numero_conta);
		} else if (pedidoThreaded->pedido.numero_conta == pedidoThreaded->pedido.conta_destino) {
			resposta.status = STATUS_CONTAS_IGUAIS;
			resposta.saldo = get_saldo(pedidoThreaded->ServerManager->sharedMemory, pedidoThreaded->pedido.numero_conta);
		} else if (get_saldo(pedidoThreaded->ServerManager->sharedMemory, pedidoThreaded->pedido.numero_conta) < pedidoThreaded->pedido.montante) {
			resposta.status = STATUS_SALDO_INSUFICIENTE;
			resposta.saldo = get_saldo(pedidoThreaded->ServerManager->sharedMemory, pedidoThreaded->pedido.numero_conta);
		} else {
			resposta.status = STATUS_OK;			
			resposta.saldo = transferencia(pedidoThreaded->ServerManager->sharedMemory, pedidoThreaded->pedido.numero_conta, pedidoThreaded->pedido.conta_destino, pedidoThreaded->pedido.montante);
		}
		break;
	case LEVANTAMENTO:
		if (!banco->aberto) {
			resposta.status = STATUS_FECHADO;
			resposta.saldo = get_saldo(pedidoThreaded->ServerManager->sharedMemory, pedidoThreaded->pedido.numero_conta);
			break;
		}
		if (get_saldo(pedidoThreaded->ServerManager->sharedMemory, pedidoThreaded->pedido.numero_conta) < pedidoThreaded->pedido.montante) {
			resposta.status = STATUS_SALDO_INSUFICIENTE;
			resposta.saldo = get_saldo(pedidoThreaded->ServerManager->sharedMemory, pedidoThreaded->pedido.numero_conta);
			break;
		} else {
			resposta.status = STATUS_OK;
			resposta.saldo = levantamento(pedidoThreaded->ServerManager->sharedMemory, pedidoThreaded->pedido.numero_conta, pedidoThreaded->pedido.montante);
			break;
		}
		break;
	default:
		break;
	}
	escreve_resposta(pedidoThreaded->ServerManager, &resposta);
	pedidoThreaded->thread_running = false;
}

void	escreve_resposta(ServerManager *ServerManager, Resposta *resposta)
{
	if (write(ServerManager->fifo_s2c, resposta, sizeof(Resposta)) == -1) {
        perror("Error writing to FIFO s2c!");
        exit(EXIT_FAILURE);
    }
}

double	get_saldo(SharedMemory *SharedMemory, int numero_conta)
{
	BancoManager *mutexes = SharedMemory->bancoManager;
	double saldo;
	pthread_mutex_lock(&mutexes->mutex_reader);
	pthread_mutex_lock(&mutexes->mutex_reader_count);
	mutexes->reader_count++;
	if (mutexes->reader_count == 1)
		pthread_mutex_lock(&mutexes->mutex_writer);
	pthread_mutex_unlock(&mutexes->mutex_reader_count);
	pthread_mutex_unlock(&mutexes->mutex_reader);
	saldo = SharedMemory->banco->contas[numero_conta].saldo;
	pthread_mutex_lock(&mutexes->mutex_reader_count);
	mutexes->reader_count--;
	if (mutexes->reader_count == 0)
		pthread_mutex_unlock(&mutexes->mutex_writer);
	pthread_mutex_unlock(&mutexes->mutex_reader_count);
	return (saldo);
}
double	deposito(SharedMemory *SharedMemory, int numero_conta, double montante)
{
	BancoManager *mutexes = SharedMemory->bancoManager;
	double saldo;

	pthread_mutex_lock(&mutexes->mutex_writer);
	while (mutexes->reader_count > 0)
		pthread_cond_wait(&mutexes->cond_writer, &mutexes->mutex_writer);
	SharedMemory->banco->contas[numero_conta].saldo += montante;
	saldo = SharedMemory->banco->contas[numero_conta].saldo;
	pthread_cond_broadcast(&mutexes->cond_writer);
	pthread_mutex_unlock(&mutexes->mutex_writer);
	return (saldo);
}
double	transferencia(SharedMemory *SharedMemory, int numero_conta, int conta_destino, double montante)
{
	BancoManager *mutexes = SharedMemory->bancoManager;
	double saldo;

	pthread_mutex_lock(&mutexes->mutex_writer);
	while (mutexes->reader_count > 0)
		pthread_cond_wait(&mutexes->cond_writer, &mutexes->mutex_writer);
	SharedMemory->banco->contas[conta_destino].saldo += montante;
	SharedMemory->banco->contas[numero_conta].saldo -= montante;
	saldo = SharedMemory->banco->contas[numero_conta].saldo;
	pthread_cond_broadcast(&mutexes->cond_writer);
	pthread_mutex_unlock(&mutexes->mutex_writer);
	return (saldo);
}
double  levantamento(SharedMemory *SharedMemory, int numero_conta, double montante)
{
	BancoManager *mutexes = SharedMemory->bancoManager;
	double saldo;

	pthread_mutex_lock(&mutexes->mutex_writer);
	while (mutexes->reader_count > 0)
		pthread_cond_wait(&mutexes->cond_writer, &mutexes->mutex_writer);
	SharedMemory->banco->contas[numero_conta].saldo -= montante;
	saldo = SharedMemory->banco->contas[numero_conta].saldo;
	pthread_cond_broadcast(&mutexes->cond_writer);
	pthread_mutex_unlock(&mutexes->mutex_writer);
	return (saldo);
}

void	init_ServerManager(ServerManager *ServerManager)
{
	int i = 0;
	if (pthread_mutex_init(&ServerManager->mutex, NULL) != 0)
	{
		perror("failed to init mutex!");
		exit(EXIT_FAILURE);
	}
	ServerManager->running = true;
	make_shared_memory(&ServerManager);
	for (i = 0; i < MAX_RUNNING_PEDIDOS; i++)
	{
		ServerManager->pedidos[i].thread_running = false;
		ServerManager->pedidos[i].ServerManager = ServerManager;
	}

}

PedidoThreaded *get_first_free_pedido(ServerManager *ServerManager, Pedido pedido)
{
	int i = 0;
	for (i = 0; i < MAX_RUNNING_PEDIDOS; i++)
		if (!ServerManager->pedidos[i].thread_running)
		{
			ServerManager->pedidos[i].pedido = pedido;
			ServerManager->pedidos[i].thread_running = true;
			return (&ServerManager->pedidos[i]);
		}
	return (NULL);
}

void	init_banco(Banco *banco, ServerManager *ServerManager)
{
	struct sigaction	s_sigaction;
	banco->aberto = true;
	for (int i = 0; i < NUM_ACCOUNTS; i++)
	{
		banco->contas[i].id = i;
		banco->contas[i].saldo = SALDO_INICIAL;
	}
	if (!init_bancoManager(ServerManager->sharedMemory.bancoManager))
		exit(EXIT_FAILURE);
	sigemptyset(&s_sigaction.sa_mask);
	s_sigaction.sa_flags = SA_SIGINFO;
	s_sigaction.sa_handler = &banco_signal_handler;
	sigaction(SIGUSR1, &s_sigaction, (void *)ServerManager);
	sigaction(SIGUSR2, &s_sigaction, (void *)ServerManager);
	sigaction(SIGINT, &s_sigaction, (void *)ServerManager);
	sigaction(SIGALRM, &s_sigaction, (void *)ServerManager);
}

bool	init_bancoManager(BancoManager *bancoManager)
{
	if (pthread_mutex_init(&bancoManager->mutex_reader, NULL) != 0)
		return (perror("failed to init mutex!"), false);
	if (pthread_mutex_init(&bancoManager->mutex_writer, NULL) != 0)
		return (perror("failed to init mutex!"), false);
	if (pthread_mutex_init(&bancoManager->mutex_reader_count, NULL) != 0)
		return (perror("failed to init mutex!"), false);
	if (pthread_cond_init(&bancoManager->cond_writer, NULL) != 0)
		return (perror("failed to init cond!"), false);
	bancoManager->reader_count = 0;
	return (true);
}

void	banco_signal_handler(int signal, siginfo_t *info, void *context) {
	ServerManager	*ServerManager = context;
	if (signal == SIGUSR1) { // fechar o banco
		ServerManager->sharedMemory.banco->aberto = false;
	} else if (signal == SIGUSR2) { // abrir o banco
		ServerManager->sharedMemory.banco->aberto = true;
	} else if (signal == SIGINT || signal == SIGALRM) { // Ctrl+C ou alarme de 2min
		printf("O banco vai fechar!\n");
		fechar_banco(ServerManager);
	}
}

void	fechar_banco(ServerManager *ServerManager)
{
	ServerManager->sharedMemory.banco->aberto = false;
	ServerManager->running = false;
	// TODO: 
	//  1. escrita no ecrã a soma de todo o dinheiro disponível no banco;
	//  2. garantida a remoção de todos os recursos (FIFOs, memória partilhada,
	//  mutexes/semáforos).
}