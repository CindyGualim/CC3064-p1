#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <cjson/cJSON.h>

#define PORT 50213
#define BUFSIZE 1024

int main() {
    int client_fd;
    struct sockaddr_in server_addr;
    char buffer[BUFSIZE];

    // 1. Crear socket
    client_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (client_fd < 0) {
        perror("Error al crear socket cliente");
        return 1;
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);

    // Convertir "127.0.0.1" a formato binario
    if (inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr) <= 0) {
        perror("Dirección inválida");
        close(client_fd);
        return 1;
    }

    // 2. Conectar
    if (connect(client_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Error al conectar con servidor");
        close(client_fd);
        return 1;
    }
    printf("[Cliente] Conectado al servidor en %s:%d\n", "127.0.0.1", PORT);

    // 3. Crear JSON de REGISTRO
    // Basado en "Organización general.pdf":
    // { "tipo":"REGISTRO", "usuario":"nombre_usuario", "direccionIP":"..." }
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "tipo", "REGISTRO");
    cJSON_AddStringToObject(root, "usuario", "Pepito");
    cJSON_AddStringToObject(root, "direccionIP", "127.0.0.1");

    // Convertir el cJSON en string
    char *jsonStr = cJSON_Print(root);

    // 4. Enviar el JSON al servidor
    send(client_fd, jsonStr, strlen(jsonStr), 0);
    printf("[Cliente] Enviado REGISTRO: %s\n", jsonStr);

    // Liberar memoria cJSON
    free(jsonStr);
    cJSON_Delete(root);

    // 5. Recibir respuesta del servidor
    memset(buffer, 0, BUFSIZE);
    int bytes = recv(client_fd, buffer, BUFSIZE-1, 0);
    if (bytes > 0) {
        printf("[Cliente] Respuesta servidor: %s\n", buffer);
    } else {
        printf("[Cliente] El servidor cerró la conexión o error.\n");
    }

    // 6. Cerrar socket
    close(client_fd);
    printf("[Cliente] Conexión cerrada.\n");
    return 0;
}
