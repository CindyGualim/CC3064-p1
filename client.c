/********************************************************
 * client.c - Cliente con menú
 *
 * OBJETIVO:
 *  - Conectarse al servidor (127.0.0.1:50213)
 *  - Registrar usuario con JSON "tipo":"REGISTRO"
 *  - Menú para BROADCAST, DM, LISTA, MOSTRAR, ESTADO, EXIT
 *
 * REFERENCIAS:
 *  - Definición de proyecto Chat 2025, v1.pdf
 *  - Organización general.pdf
 *
 * COMPILAR:
 *   gcc client.c -o client -lcjson -lpthread
 ********************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <cjson/cJSON.h>
#include <pthread.h>

#define BUFSIZE 1024

/********************************************************
 * receiveMessages()
 * Hilo que escucha constantemente los mensajes del servidor.
 ********************************************************/
void *receiveMessages(void *sock_desc) {
    int sock = *((int *)sock_desc);
    char buffer[BUFSIZE];

    while (1) {
        memset(buffer, 0, BUFSIZE);
        int bytes = recv(sock, buffer, BUFSIZE, 0);
        if (bytes <= 0) {
            // Conexión cerrada o error
            break;
        }

        // Parsear el JSON
        cJSON *root = cJSON_Parse(buffer);
        if (!root) {
            printf("[Error] JSON inválido del servidor.\n");
            continue;
        }

        // El servidor puede usar "accion" o "tipo"
        cJSON *accion = cJSON_GetObjectItem(root, "accion");
        cJSON *tipo   = cJSON_GetObjectItem(root, "tipo");

        // 1) Revisar "accion"
        if (accion && cJSON_IsString(accion)) {
            // Ej. "LISTA"
            if (strcmp(accion->valuestring, "LISTA") == 0) {
                cJSON *users = cJSON_GetObjectItem(root, "usuarios");
                if (users && cJSON_IsArray(users)) {
                    printf("\n=== CONNECTED USERS ===\n");
                    int userCount = cJSON_GetArraySize(users);
                    for (int i = 0; i < userCount; i++) {
                        cJSON *user = cJSON_GetArrayItem(users, i);
                        printf("- %s\n", user->valuestring);
                    }
                    printf("========================\n");
                } else {
                    printf("[Server] Error al recibir lista de usuarios.\n");
                }
            } else {
                // Si el servidor manda algo con "accion" distinto de LISTA
                printf("[Server]: %s\n", buffer);
            }
        }
        // 2) Revisar "tipo"
        else if (tipo && cJSON_IsString(tipo)) {
            if (strcmp(tipo->valuestring, "MOSTRAR") == 0) {
                // Ej. { "tipo":"MOSTRAR","usuario":"Cindy","estado":"ACTIVO" }
                cJSON *usuario = cJSON_GetObjectItem(root, "usuario");
                cJSON *estado  = cJSON_GetObjectItem(root, "estado");

                if (usuario && cJSON_IsString(usuario) &&
                    estado && cJSON_IsString(estado)) {
                    printf("\n=== INFO USUARIO ===\n");
                    printf("Usuario: %s\n", usuario->valuestring);
                    printf("Estado : %s\n", estado->valuestring);
                    printf("====================\n");
                } else {
                    // Puede ser un error como:
                    // {"respuesta":"ERROR","razon":"USUARIO_NO_ENCONTRADO"}
                    cJSON *respuesta = cJSON_GetObjectItem(root, "respuesta");
                    cJSON *razon     = cJSON_GetObjectItem(root, "razon");
                    if (respuesta && cJSON_IsString(respuesta) &&
                        strcmp(respuesta->valuestring, "ERROR") == 0 &&
                        razon && cJSON_IsString(razon)) {
                        printf("[Server] MOSTRAR Error: %s\n", razon->valuestring);
                    } else {
                        printf("[Server] Mensaje MOSTRAR desconocido: %s\n", buffer);
                    }
                }
            } else {
                // Otros "tipo": REGISTRO, ESTADO, etc.
                // El servidor podría mandar algo con "tipo":"REGISTRO" (aunque normalmente no).
                printf("[Server] Mensaje tipo desconocido: %s\n", buffer);
            }
        }
        else {
            // Mensaje genérico
            printf("[Server]: %s\n", buffer);
        }

        cJSON_Delete(root);
    }
    return NULL;
}

int main(int argc, char *argv[]) {
    if (argc < 4) {
        printf("Uso: %s <nombreUsuario> <IPdelservidor> <puertodelservidor>\n", argv[0]);
        return 1;
    }

    char *nombreUsuario = argv[1];
    char *ipServidor    = argv[2];
    int puertoServidor  = atoi(argv[3]);

    // Crear socket
    int client_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (client_fd < 0) {
        perror("socket");
        return 1;
    }

    // Conectar
    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family      = AF_INET;
    serv_addr.sin_port        = htons(puertoServidor);

    if (inet_pton(AF_INET, ipServidor, &serv_addr.sin_addr) <= 0) {
        perror("inet_pton");
        close(client_fd);
        return 1;
    }
    if (connect(client_fd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("connect");
        close(client_fd);
        return 1;
    }

    // Lanzar hilo de recepción
    pthread_t hiloRecepcion;
    pthread_create(&hiloRecepcion, NULL, receiveMessages, &client_fd);
    pthread_detach(hiloRecepcion);

    // Enviar REGISTRO (sin bloquear la ejecución principal)
    {
        cJSON *regJson = cJSON_CreateObject();
        cJSON_AddStringToObject(regJson, "tipo", "REGISTRO");
        cJSON_AddStringToObject(regJson, "usuario", nombreUsuario);
        cJSON_AddStringToObject(regJson, "direccionIP", ipServidor);

        char *strReg = cJSON_Print(regJson);
        send(client_fd, strReg, strlen(strReg), 0);
        free(strReg);
        cJSON_Delete(regJson);

        char buffer[BUFSIZE];
        int bytes = recv(client_fd, buffer, BUFSIZE - 1, MSG_DONTWAIT);  // 👈 No bloquea
        if (bytes > 0) {
            buffer[bytes] = '\0';
            printf("[Servidor]: %s\n", buffer); 
        }
    }

    usleep(200000);
    // Bucle principal (menú)
    while (1) {
        // Mostramos el menú
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
        opcion[strcspn(opcion, "\n")] = 0;

        // Dependiendo de la opción, enviamos la solicitud
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

            // Espera breve para que el hilo de recepción
            // muestre antes de reimprimir menú.
            usleep(300000);

            free(strJson);
            cJSON_Delete(bcast);

        } else if (strcmp(opcion, "2") == 0) {
            // DM
            char dest[50], msg[BUFSIZE];
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

            usleep(300000);

            free(strJson);
            cJSON_Delete(dm);

        } else if (strcmp(opcion, "3") == 0) {
            // LISTA
            cJSON *lst = cJSON_CreateObject();
            cJSON_AddStringToObject(lst, "accion", "LISTA");
            cJSON_AddStringToObject(lst, "nombre_usuario", nombreUsuario);

            char *strJson = cJSON_Print(lst);
            send(client_fd, strJson, strlen(strJson), 0);

            // Pausa breve para que el mensaje se reciba 
            // y se muestre antes de reimprimir el menú.
            usleep(300000);

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

            usleep(300000);

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

            char *estStr = cJSON_Print(est);
            send(client_fd, estStr, strlen(estStr), 0);

            usleep(300000);

            free(estStr);
            cJSON_Delete(est);

        } else if (strcmp(opcion, "6") == 0) {
            // EXIT
            cJSON *ex = cJSON_CreateObject();
            cJSON_AddStringToObject(ex, "tipo", "EXIT");
            cJSON_AddStringToObject(ex, "usuario", nombreUsuario);

            char *exStr = cJSON_Print(ex);
            send(client_fd, exStr, strlen(exStr), 0);
            free(exStr);
            cJSON_Delete(ex);

            close(client_fd);
            printf("Saliendo...\n");
            return 0;

        } else {
            printf("Opción inválida\n");
        }
    }

    return 0;
}
