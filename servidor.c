#include "servidor.h"

int	main(void)
{
	ServerManager	*ServerManager = get_serverManager();
	ServerManager->timer = TWO_MIN_IMILLIS;

	init_ServerManager(ServerManager);
	open_fifo_channels(ServerManager);
	init_banco(ServerManager->sharedMemory.banco, ServerManager);
	printf("pid: %d\n", getpid());
	start_timer(ServerManager);
	wait_for_pedidos(ServerManager);
	wait_for_all_threads(ServerManager);
	show_banco(&ServerManager->sharedMemory);
	close_fifo_channels(ServerManager);
	unmap_shared_memory(ServerManager);
}

ServerManager	*get_serverManager(void)
{
	static ServerManager ServerManager;
	return (&ServerManager);
}

bool	make_shared_memory(ServerManager *ServerManager)
{
	ServerManager->sharedMemory.shmid = shm_open(SHM_NAME, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
	if (ServerManager->sharedMemory.shmid == TEM_ERRO)
		return (perror("failed creating shared memory!"), false);
	if (ftruncate(ServerManager->sharedMemory.shmid, sizeof(Banco)) == TEM_ERRO)
        return (perror("Erro ao configurar o tamanho do objeto de memória compartilhada"), false);
	ServerManager->sharedMemory.banco = mmap(NULL, sizeof(Banco), PROT_READ | PROT_WRITE, MAP_SHARED, ServerManager->sharedMemory.shmid, 0);
    if (ServerManager->sharedMemory.banco == MAP_FAILED)
        return (perror("Erro ao mapear o objeto de memória compartilhada"), false);
	return (true);
}
void	unmap_shared_memory(ServerManager *ServerManager)
{
	if (munmap(ServerManager->sharedMemory.banco, sizeof(Banco)) == TEM_ERRO)
        perror("Erro ao desmapear o objeto de memória compartilhada");
	close(ServerManager->sharedMemory.shmid);
	shm_unlink(SHM_NAME);
}
void	open_fifo_channels(ServerManager *ServerManager)
{
	if (mkfifo(CLIENT_TO_SERVER_FIFO, USER_LEVEL_PERMISSIONS) == TEM_ERRO)
		perror("failed mkfifo c2s");
	if (mkfifo(SERVER_TO_CLIENT_FIFO, USER_LEVEL_PERMISSIONS) == TEM_ERRO)
		perror("failed mkfifo s2c");
	printf("Waiting for clients...\n");
	ServerManager->fifo_c2s = open(CLIENT_TO_SERVER_FIFO, O_RDONLY | O_NONBLOCK);
	if (ServerManager->fifo_c2s == TEM_ERRO)
		perror("failed open c2s");
	ServerManager->fifo_s2c = open(SERVER_TO_CLIENT_FIFO, O_WRONLY);	
	if (ServerManager->fifo_s2c == TEM_ERRO)
		perror("failed open s2c");
}
void	close_fifo_channels(ServerManager *ServerManager)
{
	close(ServerManager->fifo_c2s);
	close(ServerManager->fifo_s2c);
	unlink(CLIENT_TO_SERVER_FIFO);
	unlink(SERVER_TO_CLIENT_FIFO);
}

void	wait_for_pedidos(ServerManager *ServerManager)
{
	Pedido		*pedidoIncoming = NULL;
	size_t		bytesRead;
	
	printf("Servidor iniciado!\n");
	pthread_mutex_lock(&ServerManager->mutex);
	do
	{
		pthread_mutex_unlock(&ServerManager->mutex);
		if (pedidoIncoming == NULL) {
			pedidoIncoming = malloc(sizeof(Pedido));
			if (!pedidoIncoming) {
				perror("failed to allocate memory!");
				exit(EXIT_FAILURE);
			}
		}
		bytesRead = read(ServerManager->fifo_c2s, pedidoIncoming, sizeof(Pedido));
		if (bytesRead == -1)
		{
			if (errno == EAGAIN)
			{
				usleep(1000);
				pthread_mutex_lock(&ServerManager->mutex);
				continue;
			}
			perror("Error reading from FIFO");
		}
		if (bytesRead > 0)
		{
			switch (pedidoIncoming->tipo)
			{
			case DEPOSITO:
				printf("Depósito na conta %d de %f\n", pedidoIncoming->numero_conta, pedidoIncoming->montante);
				break;
			case TRANSFERENCIA:
				printf("Transferência da conta %d para a conta %d de %f\n", pedidoIncoming->numero_conta, pedidoIncoming->conta_destino, pedidoIncoming->montante);
				break;
			case LEVANTAMENTO:
				printf("Levantamento da conta %d de %f\n", pedidoIncoming->numero_conta, pedidoIncoming->montante);
				break;
			case CONSULTA:
				printf("Consulta da conta %d\n", pedidoIncoming->numero_conta);
				break;			
			default:
				break;
			}
			PedidoThreaded *pedidoThreaded = get_first_free_pedido(ServerManager, pedidoIncoming);
			pedidoIncoming = NULL;//will be handled by the thread
			pthread_create(&pedidoThreaded->thread, NULL, run_pedido, pedidoThreaded);
		}
		pthread_mutex_lock(&ServerManager->mutex);
	} while (ServerManager->running);
	if (pedidoIncoming != NULL)
		free(pedidoIncoming);
	pthread_mutex_unlock(&ServerManager->mutex);
}

void	wait_for_all_threads(ServerManager *ServerManager)
{
	pthread_join(ServerManager->timer_thread, NULL);
	int i = 0;
	for (i = 0; i < MAX_RUNNING_PEDIDOS; i++)
		if (ServerManager->pedidos[i].has_ran)
		{
			printf("Waiting for thread %d\n", i);
			pthread_join(ServerManager->pedidos[i].thread, NULL);
		}
}

void	*run_pedido(void *arg)
{
	PedidoThreaded	*pedidoThreaded = (PedidoThreaded *)arg;
	Resposta		resposta;
	Banco			*banco = pedidoThreaded->ServerManager->sharedMemory.banco;

	memset(&resposta, 0, sizeof(Resposta));
	resposta.pedido = *pedidoThreaded->pedido;

	switch (pedidoThreaded->pedido->tipo)
	{
	case CONSULTA:
		resposta.status = STATUS_OK;
		resposta.saldo = get_saldo(&pedidoThreaded->ServerManager->sharedMemory, pedidoThreaded->pedido->numero_conta);
		break;
	case DEPOSITO:
		pthread_mutex_lock(&pedidoThreaded->ServerManager->mutex);
		if (!banco->aberto) {
			pthread_mutex_unlock(&pedidoThreaded->ServerManager->mutex);
			resposta.status = STATUS_FECHADO;
			resposta.saldo = get_saldo(&pedidoThreaded->ServerManager->sharedMemory, pedidoThreaded->pedido->numero_conta);
			break;
		}
		resposta.status = STATUS_OK;
		resposta.saldo = deposito(&pedidoThreaded->ServerManager->sharedMemory, pedidoThreaded->pedido->numero_conta, pedidoThreaded->pedido->montante);
		pthread_mutex_unlock(&pedidoThreaded->ServerManager->mutex);
		break;
	case TRANSFERENCIA:
		pthread_mutex_lock(&pedidoThreaded->ServerManager->mutex);
		if (!banco->aberto) {
			pthread_mutex_unlock(&pedidoThreaded->ServerManager->mutex);
			resposta.status = STATUS_FECHADO;
			resposta.saldo = get_saldo(&pedidoThreaded->ServerManager->sharedMemory, pedidoThreaded->pedido->numero_conta);
		} else if (pedidoThreaded->pedido->numero_conta == pedidoThreaded->pedido->conta_destino) {
			resposta.status = STATUS_CONTAS_IGUAIS;
			resposta.saldo = get_saldo(&pedidoThreaded->ServerManager->sharedMemory, pedidoThreaded->pedido->numero_conta);
		} else if (get_saldo(&pedidoThreaded->ServerManager->sharedMemory, pedidoThreaded->pedido->numero_conta) < pedidoThreaded->pedido->montante) {
			resposta.status = STATUS_SALDO_INSUFICIENTE;
			resposta.saldo = get_saldo(&pedidoThreaded->ServerManager->sharedMemory, pedidoThreaded->pedido->numero_conta);
		} else {
			resposta.status = STATUS_OK;			
			resposta.saldo = transferencia(&pedidoThreaded->ServerManager->sharedMemory, pedidoThreaded->pedido->numero_conta, pedidoThreaded->pedido->conta_destino, pedidoThreaded->pedido->montante);
			usleep(CRITIC_WAIT_TIME_IMILLIS);// 100 milliseconds
		}
		pthread_mutex_unlock(&pedidoThreaded->ServerManager->mutex);
		break;
	case LEVANTAMENTO:
		pthread_mutex_lock(&pedidoThreaded->ServerManager->mutex);
		if (!banco->aberto) {
			pthread_mutex_unlock(&pedidoThreaded->ServerManager->mutex);
			resposta.status = STATUS_FECHADO;
			resposta.saldo = get_saldo(&pedidoThreaded->ServerManager->sharedMemory, pedidoThreaded->pedido->numero_conta);
			break;
		}
		pthread_mutex_unlock(&pedidoThreaded->ServerManager->mutex);
		if (get_saldo(&pedidoThreaded->ServerManager->sharedMemory, pedidoThreaded->pedido->numero_conta) < pedidoThreaded->pedido->montante) {
			resposta.status = STATUS_SALDO_INSUFICIENTE;
			resposta.saldo = get_saldo(&pedidoThreaded->ServerManager->sharedMemory, pedidoThreaded->pedido->numero_conta);
		} else {
			resposta.status = STATUS_OK;
			resposta.saldo = levantamento(&pedidoThreaded->ServerManager->sharedMemory, pedidoThreaded->pedido->numero_conta, pedidoThreaded->pedido->montante);
		}
		break;
	default:
		break;
	}
	escreve_resposta(pedidoThreaded->ServerManager, &resposta);
	free(pedidoThreaded->pedido);
	pedidoThreaded->pedido = NULL;
	pedidoThreaded->thread_running = false;
}

void	*timer_thread(void *arg)
{
	ServerManager *ServerManager = arg;

	int start = get_time();
	pthread_mutex_lock(&ServerManager->mutex);
	while (ServerManager->running && (get_time() - start) < ServerManager->timer)
	{
		pthread_mutex_unlock(&ServerManager->mutex);
		usleep(1000 * 50);// 50 milliseconds
		pthread_mutex_lock(&ServerManager->mutex);
	}
	pthread_mutex_unlock(&ServerManager->mutex);
	fechar_banco(ServerManager);
}

void	escreve_resposta(ServerManager *ServerManager, Resposta *resposta)
{
	if (write(ServerManager->fifo_s2c, resposta, sizeof(Resposta)) == TEM_ERRO) {
        perror("Error writing to FIFO s2c!");
        exit(EXIT_FAILURE);
    }
}

double	get_saldo(SharedMemory *SharedMemory, int numero_conta)
{
	BancoManager mutexes = SharedMemory->bancoManager;
	double saldo;
	pthread_mutex_lock(&mutexes.mutex_reader);
	pthread_mutex_lock(&mutexes.mutex_reader_count);
	mutexes.reader_count++;
	if (mutexes.reader_count == 1)
		pthread_mutex_lock(&mutexes.mutex_writer);
	pthread_mutex_unlock(&mutexes.mutex_reader_count);
	pthread_mutex_unlock(&mutexes.mutex_reader);
	saldo = SharedMemory->banco->contas[numero_conta].saldo;
	pthread_mutex_lock(&mutexes.mutex_reader_count);
	mutexes.reader_count--;
	if (mutexes.reader_count == 0)
		pthread_mutex_unlock(&mutexes.mutex_writer);
	pthread_mutex_unlock(&mutexes.mutex_reader_count);
	return (saldo);
}
double	deposito(SharedMemory *SharedMemory, int numero_conta, double montante)
{
	BancoManager *mutexes = &SharedMemory->bancoManager;
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
	BancoManager *mutexes = &SharedMemory->bancoManager;
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
	BancoManager *mutexes = &SharedMemory->bancoManager;
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
	ServerManager->fifo_c2s = 0;
	ServerManager->fifo_s2c = 0;
	make_shared_memory(ServerManager);
	for (i = 0; i < MAX_RUNNING_PEDIDOS; i++)
	{
		ServerManager->pedidos[i].thread_running = false;
		ServerManager->pedidos[i].ServerManager = ServerManager;
	}

}

PedidoThreaded *get_first_free_pedido(ServerManager *ServerManager, Pedido *pedido)
{
	int i = 0;
	for (i = 0; i < MAX_RUNNING_PEDIDOS; i++)
		if (!ServerManager->pedidos[i].thread_running)
		{
			if (ServerManager->pedidos[i].has_ran)
				pthread_join(ServerManager->pedidos[i].thread, NULL);
			ServerManager->pedidos[i].pedido = pedido;
			ServerManager->pedidos[i].thread_running = true;
			ServerManager->pedidos[i].has_ran = true;
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
	if (!init_bancoManager(&ServerManager->sharedMemory.bancoManager))
		exit(EXIT_FAILURE);
	sigemptyset(&s_sigaction.sa_mask);
	s_sigaction.sa_flags = SA_RESTART;
	s_sigaction.sa_sigaction = banco_signal_handler;
	sigaction(SIGUSR1, &s_sigaction, NULL);
	sigaction(SIGUSR2, &s_sigaction, NULL);
	sigaction(SIGINT, &s_sigaction, NULL);
	sigaction(SIGALRM, &s_sigaction, NULL);
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
	ServerManager	*ServerManager = get_serverManager();
	if (ServerManager == NULL)
	{
		perror("ServerManager is NULL!\n");
		exit(EXIT_FAILURE);
	}
	if (signal == SIGUSR1) { // fechar o banco
		printf("O banco esta agora fechado!\n");
		pthread_mutex_lock(&ServerManager->mutex);
		ServerManager->sharedMemory.banco->aberto = false;
		pthread_mutex_unlock(&ServerManager->mutex);
	} else if (signal == SIGUSR2) { // abrir o banco
		printf("O banco esta agora aberto!\n");
		pthread_mutex_lock(&ServerManager->mutex);
		ServerManager->sharedMemory.banco->aberto = true;
		pthread_mutex_unlock(&ServerManager->mutex);
	} else if (signal == SIGINT || signal == SIGALRM) { // Ctrl+C ou alarme de 2min
		printf("O banco vai encerrar por hoje!\n");
		fechar_banco(ServerManager);
	}
}

void	fechar_banco(ServerManager *ServerManager)
{
	pthread_mutex_lock(&ServerManager->mutex);
	ServerManager->running = false;
	pthread_mutex_unlock(&ServerManager->mutex);
}

void	show_banco(SharedMemory *SharedMemory)
{
	BancoManager mutexes = SharedMemory->bancoManager;
	int i = 0;
	double total = 0;

	for (i = 0; i < NUM_ACCOUNTS; i++)
	{
		//printf("Conta %d: %.3f\n", i, SharedMemory->banco->contas[i].saldo);
		total += SharedMemory->banco->contas[i].saldo;
	}
	printf("Total: %.3f\n", total);
}

int		get_time(void)
{
	static struct timeval	t;

	gettimeofday(&t, NULL);
	return ((t.tv_sec * 1000) + (t.tv_usec / 1000));
}

void	start_timer(ServerManager *ServerManager)
{
	if (pthread_create(&ServerManager->timer_thread, NULL, timer_thread, ServerManager) != 0)
	{
		perror("failed to create timer thread!");
		exit(EXIT_FAILURE);
	}
}