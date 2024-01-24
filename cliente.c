#include "cliente.h"

int main() {
	ClientManager ClientManager;

	if (pthread_mutex_init(&ClientManager.mutex, NULL) != 0) {
		printf("Erro ao inicializar o mutex!\n");
		return 1;
	}

	load_fifo_channels(&ClientManager);
	pthread_create(&ClientManager.answer_thread, NULL, answer_thread, &ClientManager);
    wait_for_input(&ClientManager);
	pthread_join(ClientManager.answer_thread, NULL);
	printf("A fechar os canais de comunicação...\n");
    close_fifo_channels(&ClientManager);
	return 0;
}

void	wait_for_input(ClientManager *ClientManager)
{
	char msg[256];
	Pedido *pedido = NULL;

	printf("Bem vindo de volta ao banco!\n");
	pthread_mutex_lock(&ClientManager->mutex);
	while (ClientManager->running) {
		pthread_mutex_unlock(&ClientManager->mutex);
		fgets(msg, 256, stdin);
		pedido = make_pedido(ClientManager, msg);
		if (pedido == NULL)
		{
			pthread_mutex_lock(&ClientManager->mutex);
			ClientManager->running = false;
			pthread_mutex_unlock(&ClientManager->mutex);
		}
		else if (!validar_pedido(pedido))
		{
			printf("Erro: numero conta invalido!\n");
			free(pedido);
		}
		else
		{
			send_pedido(ClientManager, pedido);
			pedido = NULL;
		}
		memset(msg, 0, sizeof(msg)); // reset input buffer
		pthread_mutex_lock(&ClientManager->mutex);
	}
	pthread_mutex_unlock(&ClientManager->mutex);
}

Pedido	*make_pedido(ClientManager *ClientManager, char *msg)
{
	Pedido *pedido = malloc(sizeof(Pedido));
	char *token;
	if (!pedido)
	{
		printf("Erro ao alocar memória!\n");
		exit(1);
	}

	token = strtok(msg, " ");
	memset(pedido, 0, sizeof(Pedido));
	switch (token[0])
	{
	case 'D':
		pedido->tipo = DEPOSITO;
		pedido->numero_conta = atoi(strtok(NULL, " "));
		pedido->montante = atof(strtok(NULL, " "));
		break;
	case 'T':
		pedido->tipo = TRANSFERENCIA;
		pedido->numero_conta = atoi(strtok(NULL, " "));
		pedido->conta_destino = atoi(strtok(NULL, " "));
		pedido->montante = atof(strtok(NULL, " "));
		break;
	case 'L':
		pedido->tipo = LEVANTAMENTO;
		pedido->numero_conta = atoi(strtok(NULL, " "));
		pedido->montante = atof(strtok(NULL, " "));
		break;
	case 'C':
		pedido->tipo = CONSULTA;
		pedido->numero_conta = atoi(strtok(NULL, " "));
		break;
	case 'X':
		printf("O banco vai fechar!\n");
		free(pedido);
		return NULL;
		break;
	default:
		printf("Erro: comando desconhecido!\n");
		free(pedido);
		return NULL;
		break;
	}
	return pedido;
}

void	send_pedido(ClientManager *ClientManager, Pedido *pedido)
{
	ssize_t nbytes = write(ClientManager->fifo_c2s, pedido, sizeof(Pedido));
	if (nbytes == -1) {
    	perror("Error writing to FIFO");
	}
	free(pedido);
}

void	load_fifo_channels(ClientManager *ClientManager)
{
	printf("A carregar os canais de comunicação...\n");
	ClientManager->fifo_c2s = open(CLIENT_TO_SERVER_FIFO, O_WRONLY);
	if (ClientManager->fifo_c2s == TEM_ERRO) {
		printf("Erro ao abrir o fifo c2s\n");
		exit(1);
	}
	ClientManager->fifo_s2c = open(SERVER_TO_CLIENT_FIFO, O_RDONLY | O_NONBLOCK);
	if (ClientManager->fifo_s2c == TEM_ERRO) {
		printf("Erro ao abrir o fifo s2c\n");
		exit(1);
	}
	ClientManager->running = true;
}

void	close_fifo_channels(ClientManager *ClientManager)
{
	close(ClientManager->fifo_c2s);
	close(ClientManager->fifo_s2c);
}

void	*answer_thread(void *arg)
{
	ClientManager *ClientManager = arg;
	Resposta resposta;
	int nbytes;

	pthread_mutex_lock(&ClientManager->mutex);
	while (ClientManager->running) {
		pthread_mutex_unlock(&ClientManager->mutex);
		nbytes = read(ClientManager->fifo_s2c, &resposta, sizeof(Resposta));
		if (nbytes == -1)
		{
			if (errno == EAGAIN)
			{
				pthread_mutex_lock(&ClientManager->mutex);
				continue;
			}
		} else if (nbytes == 0)
		{
			printf("O banco encerrou!\n");
			pthread_mutex_lock(&ClientManager->mutex);
			ClientManager->running = false;
			continue;
		}
		switch (resposta.status) {
			case STATUS_OK:
				switch (resposta.pedido.tipo)
				{
				case DEPOSITO:
					printf("Depósito na conta %d de %.3f, saldo atual: %.3f\n", resposta.pedido.numero_conta, resposta.pedido.montante, resposta.saldo);
					break;
				case TRANSFERENCIA:
					printf("Transferência da conta %d para a conta %d de %.3f, saldo atual: %.3f\n", resposta.pedido.numero_conta, resposta.pedido.conta_destino, resposta.pedido.montante, resposta.saldo);
					break;
				case LEVANTAMENTO:
					printf("Levantamento da conta %d de %.3f, saldo atual: %.3f\n", resposta.pedido.numero_conta, resposta.pedido.montante, resposta.saldo);
					break;
				case CONSULTA:
					printf("Consulta da conta %d, saldo: %.3f\n", resposta.pedido.numero_conta, resposta.saldo);
					break;
				default:
					printf("Erro: tipo de pedido desconhecido!\n");
					break;
				}
				break;
			case STATUS_SALDO_INSUFICIENTE:
				printf("Saldo insuficiente! Saldo: %.3f\n", resposta.saldo);
				break;
			case STATUS_FECHADO:
				printf("O banco está fechado! Saldo: %.3f\n", resposta.saldo);
				break;
			case STATUS_CONTAS_IGUAIS:
				printf("As contas são iguais\n");
				break;
			default:
				printf("Erro: status da resposta desconhecido!\n");
				break;
		}
		pthread_mutex_lock(&ClientManager->mutex);
	}
	pthread_mutex_unlock(&ClientManager->mutex);
	pthread_exit(NULL);
}

bool	validar_pedido(Pedido *pedido)
{
	if (pedido->numero_conta < 0 || pedido->numero_conta >= NUM_ACCOUNTS)
		return false;
	if (pedido->conta_destino < 0 || pedido->conta_destino >= NUM_ACCOUNTS)
		return false;
	return true;
}