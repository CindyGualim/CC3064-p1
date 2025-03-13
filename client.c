//Se conecta al servidor.
//Envía JSON para “REGISTRO”, “LISTA”, “DM”, “BROADCAST”, “EXIT”, “ESTADO”, etc.
//Recibe respuestas o mensajes de otros usuarios (vía servidor).
//Muestra la “interfaz” en consola (texto simple, menú, o algo más elaborado).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define PORT 50213       // Puerto donde escucha el servidor
#define BUFSIZE 1024

int main() {
    int client_fd;
    struct sockaddr_in server_addr;
    char buffer[BUFSIZE];
    int recv_bytes;

    /*************************************************
     * 1. Crear socket TCP
     *************************************************/
    client_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (client_fd < 0) {
        perror("Error al crear socket del cliente");
        exit(EXIT_FAILURE);
    }
    printf("Socket del cliente creado correctamente.\n");

    // Llenar la estructura de dirección del servidor
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    // inet_pton( ) convierte la IP en binario para sin_addr
    if (inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr) <= 0) {
        perror("Dirección inválida o no soportada");
        close(client_fd);
        exit(EXIT_FAILURE);
    }

    /*************************************************
     * 2. Conectarse al servidor
     *************************************************/
    if (connect(client_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Error al conectar con el servidor");
        close(client_fd);
        exit(EXIT_FAILURE);
    }
    printf("Conectado al servidor en 127.0.0.1:%d.\n", PORT);

    /*************************************************
     * 3. Enviar un mensaje al servidor
     *************************************************/
    const char *msg_cliente = "Hola servidor, soy el cliente.\n";
    send(client_fd, msg_cliente, strlen(msg_cliente), 0);
    printf("Mensaje enviado al servidor.\n");

    /*************************************************
     * 4. Recibir la respuesta del servidor
     *************************************************/
    memset(buffer, 0, BUFSIZE);
    recv_bytes = recv(client_fd, buffer, BUFSIZE - 1, 0);
    if (recv_bytes < 0) {
        perror("Error al recibir respuesta del servidor");
    } else if (recv_bytes == 0) {
        printf("El servidor cerró la conexión.\n");
    } else {
        printf("Respuesta del servidor: %s\n", buffer);
    }

    /*************************************************
     * 5. Cerrar el socket del cliente
     *************************************************/
    close(client_fd);
    printf("Conexión con el servidor cerrada.\n");

    return 0;
}
