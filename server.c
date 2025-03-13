#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>       // Para hilos: pthread_create, etc.
#include <sys/socket.h>    // Para sockets
#include <netinet/in.h>    // Para struct sockaddr_in
#include <arpa/inet.h>     // Para inet_pton, etc.
#include <cjson/cJSON.h>   // Librería cJSON

#define PORT 50213
#define BACKLOG 10         // Máx. conexiones en cola
#define BUFSIZE 1024
#define MAX_CLIENTS 10     // Máx. usuarios en nuestro array

/***********************************************************
 * ESTRUCTURA PARA USUARIOS CONECTADOS
 ***********************************************************/
typedef struct {
    int socketFD;
    char nombre[50];
    char ip[50];
    int activo;  // 1 si está conectado; 0 si está libre la posición
} Cliente;

/***********************************************************
 * LISTA GLOBAL DE CLIENTES + MUTEX
 ***********************************************************/
static Cliente clientesConectados[MAX_CLIENTS];
static pthread_mutex_t clientesMutex = PTHREAD_MUTEX_INITIALIZER;

/***********************************************************
 * FUNCIÓN: registrarUsuario()
 * Recibe: nombre de usuario, ip y socket.
 * 1) Verifica si ya existe el usuario o la IP en la lista.
 * 2) Si no existe, lo inserta.
 * Devuelve: 0 = éxito (OK), -1 = error (usuario/ip duplicado).
 ***********************************************************/
int registrarUsuario(const char *nombre, const char *ip, int socketFD) {
    pthread_mutex_lock(&clientesMutex);

    // Verificar duplicados
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clientesConectados[i].activo == 1) {
            if (strcmp(clientesConectados[i].nombre, nombre) == 0) {
                // El nombre ya existe
                pthread_mutex_unlock(&clientesMutex);
                return -1;
            }
            if (strcmp(clientesConectados[i].ip, ip) == 0) {
                // La ip ya está asignada a otro usuario
                pthread_mutex_unlock(&clientesMutex);
                return -1;
            }
        }
    }

    // Buscar un espacio libre
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clientesConectados[i].activo == 0) {
            clientesConectados[i].socketFD = socketFD;
            strcpy(clientesConectados[i].nombre, nombre);
            strcpy(clientesConectados[i].ip, ip);
            clientesConectados[i].activo = 1;
            pthread_mutex_unlock(&clientesMutex);
            return 0; // éxito
        }
    }

    pthread_mutex_unlock(&clientesMutex);
    return -1; // no había espacio para nuevos usuarios
}

/***********************************************************
 * FUNCIÓN: responderError()
 * Crea y envía un JSON con {"respuesta":"ERROR","razon":"X"}
 ***********************************************************/
void responderError(int socketFD, const char *razon) {
    cJSON *resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "respuesta", "ERROR");
    cJSON_AddStringToObject(resp, "razon", razon);

    char *strResp = cJSON_Print(resp);
    send(socketFD, strResp, strlen(strResp), 0);

    free(strResp);
    cJSON_Delete(resp);
}

/***********************************************************
 * FUNCIÓN: responderOK()
 * Crea y envía un JSON con {"respuesta":"OK"}
 ***********************************************************/
void responderOK(int socketFD) {
    cJSON *resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "respuesta", "OK");

    char *strResp = cJSON_Print(resp);
    send(socketFD, strResp, strlen(strResp), 0);

    free(strResp);
    cJSON_Delete(resp);
}

/***********************************************************
 * FUNCIÓN DEL HILO: manejarCliente()
 * Cada cliente conectado cae aquí en su propio hilo.
 * Lee mensajes, los parsea como JSON y maneja "REGISTRO".
 ***********************************************************/
void* manejarCliente(void *arg) {
    int clientFD = *(int*)arg;
    free(arg);  // liberamos la memoria reservada para el FD

    char buffer[BUFSIZE];
    int bytesRecibidos;

    printf("[Hilo] Nuevo cliente manejado en FD=%d\n", clientFD);

    while (1) {
        memset(buffer, 0, BUFSIZE);
        bytesRecibidos = recv(clientFD, buffer, BUFSIZE - 1, 0);
        if (bytesRecibidos <= 0) {
            // Error o conexión cerrada
            printf("[Hilo] Cliente FD=%d desconectado.\n", clientFD);
            close(clientFD);
            pthread_exit(NULL);
        }

        // Parsear JSON con cJSON
        cJSON *root = cJSON_Parse(buffer);
        if (!root) {
            // No es un JSON válido
            printf("[Hilo] Mensaje no es JSON: %s\n", buffer);
            responderError(clientFD, "JSON_INVALIDO");
            continue;
        }

        // Obtener el campo "tipo"
        cJSON *tipo = cJSON_GetObjectItemCaseSensitive(root, "tipo");
        if (!cJSON_IsString(tipo)) {
            responderError(clientFD, "FALTA_TIPO");
            cJSON_Delete(root);
            continue;
        }

        // Comparar el valor de "tipo"
        if (strcmp(tipo->valuestring, "REGISTRO") == 0) {
            // Sacar "usuario" y "direccionIP"
            cJSON *usuario = cJSON_GetObjectItemCaseSensitive(root, "usuario");
            cJSON *direccionIP = cJSON_GetObjectItemCaseSensitive(root, "direccionIP");
            if (!cJSON_IsString(usuario) || !cJSON_IsString(direccionIP)) {
                responderError(clientFD, "FALTAN_CAMPOS_REGISTRO");
            } else {
                // Intentar registrar
                if (registrarUsuario(usuario->valuestring, direccionIP->valuestring, clientFD) == 0) {
                    responderOK(clientFD);
                    printf("[Hilo] Usuario '%s' registrado con IP %s en FD=%d\n",
                           usuario->valuestring, direccionIP->valuestring, clientFD);
                } else {
                    responderError(clientFD, "USUARIO_O_IP_DUPLICADO");
                }
            }
        }
        else {
            // Tipo no implementado aún
            responderError(clientFD, "TIPO_NO_IMPLEMENTADO");
        }

        cJSON_Delete(root);
    }

    return NULL;
}

/***********************************************************
 * FUNCIÓN PRINCIPAL: main()
 * Crea socket, bind, listen, acepta conexiones en bucle.
 * Por cada cliente, lanza un hilo manejarCliente().
 ***********************************************************/
int main() {
    int server_fd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);

    // Inicializar array de clientes
    for (int i = 0; i < MAX_CLIENTS; i++) {
        clientesConectados[i].activo = 0;
    }

    // 1. Crear socket
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("Error al crear socket");
        exit(EXIT_FAILURE);
    }
    printf("[Servidor] Socket creado.\n");

    // Configurar server_addr
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    // 2. Bind
    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Error en bind");
        close(server_fd);
        exit(EXIT_FAILURE);
    }
    printf("[Servidor] Bind exitoso en puerto %d.\n", PORT);

    // 3. Listen
    if (listen(server_fd, BACKLOG) < 0) {
        perror("Error en listen");
        close(server_fd);
        exit(EXIT_FAILURE);
    }
    printf("[Servidor] Esperando conexiones...\n");

    while (1) {
        // 4. Accept (en bucle)
        int *nuevoFD = malloc(sizeof(int)); // se libera en manejarCliente
        *nuevoFD = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
        if (*nuevoFD < 0) {
            perror("Error en accept");
            free(nuevoFD);
            continue; 
        }
        printf("[Servidor] Cliente conectado. FD=%d\n", *nuevoFD);

        // 5. Crear hilo para manejar al cliente
        pthread_t tid;
        if (pthread_create(&tid, NULL, manejarCliente, nuevoFD) != 0) {
            perror("Error al crear hilo");
            close(*nuevoFD);
            free(nuevoFD);
        } else {
            pthread_detach(tid); // el hilo se autolibera al terminar
        }
    }
    return 0;
}
