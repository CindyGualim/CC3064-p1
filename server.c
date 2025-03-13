//Escucha en el puerto (Por convención, 50213)
//acepta conexiones entrantes (inicialmente uno; luego múltiples con threads).
//Lleva registro de usuarios en una estructura compartida (manejo de hilos + mutex).
//Interpreta y responde mensajes JSON (tipo "REGISTRO", "DM", "BROADCAST", etc.).
//Notifica a los demás clientes en caso de broadcasting, etc.

//librerías de sockets y standard
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

//puerto sugerido 
#define PORT 50213
#define BACKLOG 5 //Máx conexiones en cola
#define BUFSIZE 1024 //Tamaño de Buffer para recibir datos

int main(){
    int server_fd, client_fd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_addr_size = sizeof(client_addr);

     char buffer[BUFSIZE];
     int recv_bytes;

     
    /*************************************************
     * 1. Crear socket TCP
     *************************************************/
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("Error al crear socket");
        exit(EXIT_FAILURE);
    }
    printf("Socket creado con éxito.\n");

    // Llenar la estructura de dirección (server_addr)
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    // INADDR_ANY -> Aceptar conexiones desde cualquier IP
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    /*************************************************
     * 2. Asociar (bind) el socket al puerto
     *************************************************/
    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Error en bind");
        close(server_fd);
        exit(EXIT_FAILURE);
    }
    printf("Bind exitoso en el puerto %d.\n", PORT);

    /*************************************************
     * 3. Poner el socket en modo escucha (listen)
     *************************************************/
    if (listen(server_fd, BACKLOG) < 0) {
        perror("Error en listen");
        close(server_fd);
        exit(EXIT_FAILURE);
    }
    printf("Escuchando conexiones entrantes...\n");

    /*************************************************
     * 4. Aceptar una conexión
     *************************************************/
    client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_addr_size);
    if (client_fd < 0) {
        perror("Error en accept");
        close(server_fd);
        exit(EXIT_FAILURE);
    }
    printf("Cliente conectado.\n");

    /*************************************************
     * 5. Recibir datos del cliente
     *************************************************/
    memset(buffer, 0, BUFSIZE);
    recv_bytes = recv(client_fd, buffer, BUFSIZE - 1, 0);
    if (recv_bytes < 0) {
        perror("Error al recibir datos del cliente");
    } else {
        printf("Mensaje recibido del cliente: %s\n", buffer);
    }

    /*************************************************
     * 6. Enviar respuesta al cliente
     *************************************************/
    const char *msg_servidor = "¡Hola desde el servidor!\n";
    send(client_fd, msg_servidor, strlen(msg_servidor), 0);

    /*************************************************
     * 7. Cerrar conexiones
     *************************************************/
    close(client_fd);   // Cierra la conexión con el cliente
    close(server_fd);   // Cierra el socket del servidor
    printf("Conexión cerrada. Servidor finalizado.\n");

    return 0;
}
