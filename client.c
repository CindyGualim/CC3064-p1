/********************************************************
 * client.c - Cliente con menú
 *
 * OBJETIVO:
 *  - Conectarse al servidor (127.0.0.1:50213)
 *  - Registrar usuario con JSON "tipo":"REGISTRO"
 *  - Menú para BROADCAST, DM, LISTA, EXIT, etc.
 *
 * COMPILAR:
 *   gcc client.c -o client -lcjson
 ********************************************************/
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

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Uso: %s <nombreUsuario>\n", argv[0]);
        return 1;
    }
    char *nombreUsuario = argv[1];
    // Asumimos IP local para pruebas
    char *ipUsuario = "127.0.0.1";

    // 1. Crear socket
    int client_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (client_fd < 0) {
        perror("socket");
        return 1;
    }

    // 2. Conectar al servidor
    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);
    if (inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr) <= 0) {
        perror("inet_pton");
        close(client_fd);
        return 1;
    }
    if (connect(client_fd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("connect");
        close(client_fd);
        return 1;
    }
    printf("[Cliente] Conectado al servidor.\n");

    // 3. Registrar con "REGISTRO"
    {
        cJSON *regJson = cJSON_CreateObject();
        cJSON_AddStringToObject(regJson, "tipo", "REGISTRO");
        cJSON_AddStringToObject(regJson, "usuario", nombreUsuario);
        cJSON_AddStringToObject(regJson, "direccionIP", ipUsuario);

        char *strReg = cJSON_Print(regJson);
        send(client_fd, strReg, strlen(strReg), 0);
        free(strReg);
        cJSON_Delete(regJson);

        // Recibir respuesta
        char buffer[BUFSIZE];
        int bytes = recv(client_fd, buffer, BUFSIZE-1, 0);
        if (bytes > 0) {
            buffer[bytes] = '\0';
            printf("Respuesta REGISTRO: %s\n", buffer);
        }
    }

    // 4. Bucle de menú
    while (1) {
        printf("\n=== MENU ===\n");
        printf("1) Broadcast\n");
        printf("2) DM\n");
        printf("3) LISTA\n");
        printf("4) MOSTRAR info usuario\n");
        printf("5) ESTADO\n");
        printf("6) EXIT\n");
        printf("Elige opción: ");
        fflush(stdout);

        char opcion[10];
        fgets(opcion, 10, stdin);

        // Quitar salto de línea
        opcion[strcspn(opcion, "\n")] = 0;

        if (strcmp(opcion, "1") == 0) {
            // BROADCAST
            printf("Mensaje a todos: ");
            char msg[BUFSIZE];
            fgets(msg, BUFSIZE, stdin);
            msg[strcspn(msg, "\n")] = 0;

            cJSON *bcast = cJSON_CreateObject();
            cJSON_AddStringToObject(bcast, "accion", "BROADCAST");
            cJSON_AddStringToObject(bcast, "nombre_emisor", nombreUsuario);
            cJSON_AddStringToObject(bcast, "mensaje", msg);

            char *strJson = cJSON_Print(bcast);
            send(client_fd, strJson, strlen(strJson), 0);
            free(strJson);
            cJSON_Delete(bcast);

        } else if (strcmp(opcion, "2") == 0) {
            // DM
            char dest[50];
            char msg[BUFSIZE];
            printf("Destinatario: ");
            fgets(dest, 50, stdin);
            dest[strcspn(dest, "\n")] = 0;

            printf("Mensaje: ");
            fgets(msg, BUFSIZE, stdin);
            msg[strcspn(msg, "\n")] = 0;

            cJSON *dm = cJSON_CreateObject();
            cJSON_AddStringToObject(dm, "accion", "DM");
            cJSON_AddStringToObject(dm, "nombre_emisor", nombreUsuario);
            cJSON_AddStringToObject(dm, "nombre_destinatario", dest);
            cJSON_AddStringToObject(dm, "mensaje", msg);

            char *strJson = cJSON_Print(dm);
            send(client_fd, strJson, strlen(strJson), 0);
            free(strJson);
            cJSON_Delete(dm);

        } else if (strcmp(opcion, "3") == 0) {
            // LISTA
            cJSON *lst = cJSON_CreateObject();
            cJSON_AddStringToObject(lst, "accion", "LISTA");
            char *strJson = cJSON_Print(lst);
            send(client_fd, strJson, strlen(strJson), 0);
            free(strJson);
            cJSON_Delete(lst);

        } else if (strcmp(opcion, "4") == 0) {
            // MOSTRAR
            char usuario[50];
            printf("Nombre de usuario a mostrar info: ");
            fgets(usuario, 50, stdin);
            usuario[strcspn(usuario, "\n")] = 0;

            cJSON *most = cJSON_CreateObject();
            cJSON_AddStringToObject(most, "tipo", "MOSTRAR");
            cJSON_AddStringToObject(most, "usuario", usuario);

            char *strJson = cJSON_Print(most);
            send(client_fd, strJson, strlen(strJson), 0);
            free(strJson);
            cJSON_Delete(most);

        } else if (strcmp(opcion, "5") == 0) {
            // ESTADO
            char nuevoEstado[20];
            printf("Nuevo estado (ACTIVO, OCUPADO, INACTIVO): ");
            fgets(nuevoEstado, 20, stdin);
            nuevoEstado[strcspn(nuevoEstado, "\n")] = 0;

            cJSON *est = cJSON_CreateObject();
            cJSON_AddStringToObject(est, "tipo", "ESTADO");
            cJSON_AddStringToObject(est, "usuario", nombreUsuario);
            cJSON_AddStringToObject(est, "estado", nuevoEstado);

            char *strJson = cJSON_Print(est);
            send(client_fd, strJson, strlen(strJson), 0);
            free(strJson);
            cJSON_Delete(est);

        } else if (strcmp(opcion, "6") == 0) {
            // EXIT
            cJSON *ex = cJSON_CreateObject();
            cJSON_AddStringToObject(ex, "tipo", "EXIT");
            cJSON_AddStringToObject(ex, "usuario", nombreUsuario);
            cJSON_AddStringToObject(ex, "estado", "");  // el PDF a veces lo incluye en EXIT

            char *strJson = cJSON_Print(ex);
            send(client_fd, strJson, strlen(strJson), 0);
            free(strJson);
            cJSON_Delete(ex);

            // Normalmente esperaríamos una respuesta "OK"
            close(client_fd);
            printf("Saliendo...\n");
            return 0;

        } else {
            printf("Opción inválida\n");
        }

        // Después de enviar, podemos leer la respuesta del servidor.
        // (Opcional: leer en un hilo aparte y mostrarlo)
        // Por simplicidad, aquí haremos un recv simple sin bucle:
        char buf[BUFSIZE];
        int bytes = recv(client_fd, buf, BUFSIZE-1, MSG_DONTWAIT);
        // MSG_DONTWAIT = no bloqueante; si no hay respuesta inmediata, sigue
        if (bytes > 0) {
            buf[bytes] = '\0';
            printf("[Servidor dice]: %s\n", buf);
        }
    }

    return 0;
}
