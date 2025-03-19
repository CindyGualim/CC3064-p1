/********************************************************
 * server.c
 *
 * Servidor multihilo para chat, con las acciones del protocolo:
 * - REGISTRO
 * - EXIT
 * - BROADCAST
 * - DM
 * - LISTA
 * - MOSTRAR
 * - ESTADO
 *
 * Referencias:
 *  - Definición de proyecto Chat 2025, v1.pdf
 *  - Organización general.pdf
 *
 * Para compilar:
 *   gcc server.c -o server -lpthread -lcjson
 ********************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <cjson/cJSON.h>
#include <ctype.h> // Para toupper

#define PORT 50213
#define BACKLOG 10
#define BUFSIZE 1024
#define MAX_CLIENTS 10

void strToUpper(char *dest, const char *src) {
    while (*src) {
        *dest = toupper((unsigned char)*src);
        dest++;
        src++;
    }
    *dest = '\0';
}

/********************************************************
 * Estructura para cada cliente en la lista global.
 ********************************************************/
typedef struct {
    int socketFD;
    char nombre[50];
    char ip[50];
    char status[10]; // "ACTIVO", "OCUPADO", "INACTIVO"
    int activo;      // 1 si está conectado, 0 si no
} Cliente;

static Cliente clientesConectados[MAX_CLIENTS];
static pthread_mutex_t clientesMutex = PTHREAD_MUTEX_INITIALIZER;

/*******************************************************
 * Funciones de respuesta en JSON (OK / ERROR / Mensaje)
 *******************************************************/
void responderOK(int socketFD) {
    cJSON *resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "respuesta", "OK");
    char *str = cJSON_Print(resp);
    send(socketFD, str, strlen(str), 0);
    free(str);
    cJSON_Delete(resp);
}
void responderError(int socketFD, const char *razon) {
    cJSON *resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "respuesta", "ERROR");
    cJSON_AddStringToObject(resp, "razon", razon);
    char *str = cJSON_Print(resp);
    send(socketFD, str, strlen(str), 0);
    free(str);
    cJSON_Delete(resp);
}
/** Para enviar un JSON cualquiera (útil en BROADCAST, etc.) */
void enviarJSON(int socketFD, cJSON *obj) {
    char *str = cJSON_Print(obj);
    send(socketFD, str, strlen(str), 0);
    free(str);
}

/********************************************************
 * registrarUsuario()
 *  - Recibe: nombre, ip y socketFD
 *  - Verifica si nombre e ip ya están en uso
 *  - Si no, lo inserta en la lista
 *  - Retorna 0 si éxito, -1 si duplicado / sin espacio
 ********************************************************/
int registrarUsuario(const char *nombre, const char *ip, int socketFD) {
    pthread_mutex_lock(&clientesMutex);

    // Revisar si ya existe usuario o IP en uso
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clientesConectados[i].activo == 1) {
            // Si el nombre existe
            if (strcmp(clientesConectados[i].nombre, nombre) == 0) {
                pthread_mutex_unlock(&clientesMutex);
                return -1; // nombre duplicado
            }
            /* 
            if (strcmp(clientesConectados[i].ip, ip) == 0) {
                pthread_mutex_unlock(&clientesMutex);
                return -1; // ip duplicada
            }
            */ //comentado para probar con ips repetidas
        }
    }

    // Buscar espacio libre
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clientesConectados[i].activo == 0) {
            clientesConectados[i].socketFD = socketFD;
            strcpy(clientesConectados[i].nombre, nombre);
            strcpy(clientesConectados[i].ip, ip);
            strcpy(clientesConectados[i].status, "ACTIVO");
            clientesConectados[i].activo = 1;

            // Mensaje en el servidor cuando un usario se registra
            printf("[SERVIDOR] Usuario registrado: %s | IP: %s | FD: %d\n",
                clientesConectados[i].nombre, clientesConectados[i].ip, socketFD);
            pthread_mutex_unlock(&clientesMutex);
            return 0; // éxito
        }
    }

    pthread_mutex_unlock(&clientesMutex);
    return -1; // sin espacio
}

/********************************************************
 * Funciones auxiliares
 ********************************************************/
int buscarClientePorFD(int fd) {
    pthread_mutex_lock(&clientesMutex);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clientesConectados[i].activo == 1 &&
            clientesConectados[i].socketFD == fd) {
            pthread_mutex_unlock(&clientesMutex);
            return i;
        }
    }
    pthread_mutex_unlock(&clientesMutex);
    return -1;
}

void liberarCliente(int fd) {
    pthread_mutex_lock(&clientesMutex);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clientesConectados[i].activo == 1 &&
            clientesConectados[i].socketFD == fd) {
            clientesConectados[i].activo = 0;
            printf("[Servidor] Liberado cliente '%s' (FD:%d)\n",
                   clientesConectados[i].nombre, fd);
            break;
        }
    }
    pthread_mutex_unlock(&clientesMutex);
}

/********************************************************
 * BROADCAST: "accion":"BROADCAST"
 ********************************************************/
void manejarBroadcast(int emisorFD, cJSON *root) {
    cJSON *nom = cJSON_GetObjectItem(root, "nombre_emisor");
    cJSON *msg = cJSON_GetObjectItem(root, "mensaje");
    if (!cJSON_IsString(nom) || !cJSON_IsString(msg)) {
        responderError(emisorFD, "FORMATO_BROADCAST_INVALIDO");
        return;
    }

    // JSON para enviar a todos
    cJSON *bcast = cJSON_CreateObject();
    cJSON_AddStringToObject(bcast, "accion", "BROADCAST");
    cJSON_AddStringToObject(bcast, "nombre_emisor", nom->valuestring);
    cJSON_AddStringToObject(bcast, "mensaje", msg->valuestring);

    pthread_mutex_lock(&clientesMutex);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clientesConectados[i].activo == 1) {
            enviarJSON(clientesConectados[i].socketFD, bcast);
        }
    }
    pthread_mutex_unlock(&clientesMutex);

    cJSON_Delete(bcast);
}

/********************************************************
 * DM: "accion":"DM"
 ********************************************************/
void manejarDM(int emisorFD, cJSON *root) {
    cJSON *nomEmisor = cJSON_GetObjectItem(root, "nombre_emisor");
    cJSON *nomDest = cJSON_GetObjectItem(root, "nombre_destinatario");
    cJSON *msg = cJSON_GetObjectItem(root, "mensaje");

    if (!cJSON_IsString(nomEmisor) || !cJSON_IsString(nomDest) || !cJSON_IsString(msg)) {
        responderError(emisorFD, "FORMATO_DM_INVALIDO");
        return;
    }

    cJSON *dm = cJSON_CreateObject();
    cJSON_AddStringToObject(dm, "accion", "DM");
    cJSON_AddStringToObject(dm, "nombre_emisor", nomEmisor->valuestring);
    cJSON_AddStringToObject(dm, "nombre_destinatario", nomDest->valuestring);
    cJSON_AddStringToObject(dm, "mensaje", msg->valuestring);

    pthread_mutex_lock(&clientesMutex);
    int encontrado = 0;
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clientesConectados[i].activo == 1 &&
            strcmp(clientesConectados[i].nombre, nomDest->valuestring) == 0) {
            enviarJSON(clientesConectados[i].socketFD, dm);
            encontrado = 1;
            break;
        }
    }
    pthread_mutex_unlock(&clientesMutex);

    if (!encontrado) {
        responderError(emisorFD, "DESTINATARIO_NO_ENCONTRADO");
    } else {
        responderOK(emisorFD);
    }
    cJSON_Delete(dm);
}

/********************************************************
 * LISTA: "accion":"LISTA"
 ********************************************************/
void manejarLista(int emisorFD) {
    cJSON *resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "accion", "LISTA");

    cJSON *arrUsuarios = cJSON_CreateArray();

    pthread_mutex_lock(&clientesMutex);
    printf("[SERVIDOR] Generando lista de usuarios conectados...\n");

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clientesConectados[i].activo == 1) {
            printf("[SERVIDOR] Usuario activo: %s\n", clientesConectados[i].nombre);
            cJSON_AddItemToArray(arrUsuarios, cJSON_CreateString(clientesConectados[i].nombre));
        }
    }
    pthread_mutex_unlock(&clientesMutex);

    cJSON_AddItemToObject(resp, "usuarios", arrUsuarios);

    char *strJson = cJSON_Print(resp);
    printf("[SERVIDOR] Enviando lista: %s\n", strJson); // Imprime en el servidor lo que se enviará
    send(emisorFD, strJson, strlen(strJson), 0);

    free(strJson);
    cJSON_Delete(resp);
}

/********************************************************
 * MOSTRAR: "tipo":"MOSTRAR"
 ********************************************************/
void manejarMostrar(int emisorFD, cJSON *root) {
    cJSON *usuario = cJSON_GetObjectItem(root, "usuario");
    if (!cJSON_IsString(usuario)) {
        responderError(emisorFD, "FORMATO_MOSTRAR_INVALIDO");
        return;
    }

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "tipo", "MOSTRAR");

    pthread_mutex_lock(&clientesMutex);
    int encontrado = 0;
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clientesConectados[i].activo == 1 &&
            strcmp(clientesConectados[i].nombre, usuario->valuestring) == 0) {
            cJSON_AddStringToObject(resp, "usuario", clientesConectados[i].nombre);
            cJSON_AddStringToObject(resp, "estado", clientesConectados[i].status);
            // cJSON_AddStringToObject(resp, "ip", clientesConectados[i].ip); // opcional
            encontrado = 1;
            break;
        }
    }
    pthread_mutex_unlock(&clientesMutex);

    if (!encontrado) {
        cJSON_AddStringToObject(resp, "respuesta", "ERROR");
        cJSON_AddStringToObject(resp, "razon", "USUARIO_NO_ENCONTRADO");
    }
    enviarJSON(emisorFD, resp);
    cJSON_Delete(resp);
}

/********************************************************
 * ESTADO: "tipo":"ESTADO"
 ********************************************************/
void manejarEstado(int emisorFD, cJSON *root) {
    cJSON *usuario = cJSON_GetObjectItem(root, "usuario");
    cJSON *estado  = cJSON_GetObjectItem(root, "estado");

    if (!cJSON_IsString(usuario) || !cJSON_IsString(estado)) {
        responderError(emisorFD, "FORMATO_ESTADO_INVALIDO");
        return;
    }

    // 1) Convertir el nuevo estado a mayúsculas
    char nuevoEstado[20];
    strToUpper(nuevoEstado, estado->valuestring);

    pthread_mutex_lock(&clientesMutex);

    int encontrado = 0;
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clientesConectados[i].activo == 1 &&
            strcmp(clientesConectados[i].nombre, usuario->valuestring) == 0) {

            // 2) Convertir el estado actual a mayúsculas antes de comparar
            char estadoActual[20];
            strToUpper(estadoActual, clientesConectados[i].status);

            // 3) Comparar ignoring-case (ya que ambos están en mayúsculas)
            if (strcmp(estadoActual, nuevoEstado) == 0) {
                pthread_mutex_unlock(&clientesMutex);
                responderError(emisorFD, "ESTADO_YA_SELECCIONADO");
                return;
            }

            // 4) Diferentes: Actualizamos y respondemos OK
            strcpy(clientesConectados[i].status, nuevoEstado); 
            encontrado = 1;
            break;
        }
    }

    pthread_mutex_unlock(&clientesMutex);

    if (!encontrado) {
        responderError(emisorFD, "USUARIO_NO_ENCONTRADO");
    } else {
        responderOK(emisorFD);
    }
}


/********************************************************
 * Manejo del hilo para cada cliente
 ********************************************************/
void* manejarCliente(void *arg) {
    int clientFD = *(int*)arg;
    free(arg);

    while (1) {
        char buffer[BUFSIZE];
        memset(buffer, 0, BUFSIZE);
        int bytes = recv(clientFD, buffer, BUFSIZE - 1, 0);
        if (bytes <= 0) {
            printf("[Hilo] Cliente FD: %d desconectado.\n", clientFD);
            close(clientFD);
            liberarCliente(clientFD);
            pthread_exit(NULL);
        }

        cJSON *root = cJSON_Parse(buffer);
        if (!root) {
            responderError(clientFD, "JSON_INVALIDO");
            continue;
        }

        // Revisar "accion" o "tipo"
        cJSON *accion = cJSON_GetObjectItem(root, "accion");
        cJSON *tipo   = cJSON_GetObjectItem(root, "tipo");

        // "accion": BROADCAST, DM, LISTA
        if (accion && cJSON_IsString(accion)) {
            if (strcmp(accion->valuestring, "BROADCAST") == 0) {
                manejarBroadcast(clientFD, root);
            } else if (strcmp(accion->valuestring, "DM") == 0) {
                manejarDM(clientFD, root);
            } else if (strcmp(accion->valuestring, "LISTA") == 0) {
                manejarLista(clientFD);
            } else {
                responderError(clientFD, "ACCION_NO_IMPLEMENTADA");
            }
        }
        // "tipo": REGISTRO, EXIT, MOSTRAR, ESTADO
        else if (tipo && cJSON_IsString(tipo)) {

            if (strcmp(tipo->valuestring, "REGISTRO") == 0) {
                // Extraer datos
                cJSON *usuario = cJSON_GetObjectItem(root, "usuario");
                cJSON *direccionIP = cJSON_GetObjectItem(root, "direccionIP");
                if (!cJSON_IsString(usuario) || !cJSON_IsString(direccionIP)) {
                    responderError(clientFD, "CAMPOS_REGISTRO_INVALIDOS");
                } else {
                    if (registrarUsuario(usuario->valuestring, direccionIP->valuestring, clientFD) == 0) {
                        responderOK(clientFD);

                    } else {
                        responderError(clientFD, "USUARIO_O_IP_DUPLICADO");
                    }
                }
            }
            else if (strcmp(tipo->valuestring, "EXIT") == 0) {
                responderOK(clientFD);
                close(clientFD);
                liberarCliente(clientFD);
                cJSON_Delete(root);
                pthread_exit(NULL);
            }
            else if (strcmp(tipo->valuestring, "MOSTRAR") == 0) {
                manejarMostrar(clientFD, root);
            }
            else if (strcmp(tipo->valuestring, "ESTADO") == 0) {
                manejarEstado(clientFD, root);
            }
            else {
                responderError(clientFD, "TIPO_NO_IMPLEMENTADO");
            }
        }
        else {
            responderError(clientFD, "FALTA_TIPO_O_ACCION");
        }

        cJSON_Delete(root);
    }

    return NULL;
}

/********************************************************
 * main()
 ********************************************************/
int main() {
    // Inicializar array
    for (int i = 0; i < MAX_CLIENTS; i++) {
        clientesConectados[i].activo = 0;
        strcpy(clientesConectados[i].status, "ACTIVO");
    }

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        exit(1);
    }

    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind");
        close(server_fd);
        exit(1);
    }

    if (listen(server_fd, BACKLOG) < 0) {
        perror("listen");
        close(server_fd);
        exit(1);
    }

    printf("[SERVIDOR] Escuchando en puerto %d...\n", PORT);

    while (1) {
        int *nuevoFD = malloc(sizeof(int));
        *nuevoFD = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
        if (*nuevoFD < 0) {
            perror("accept");
            free(nuevoFD);
            continue;
        }
        // Lanza hilo para manejar al cliente
        pthread_t tid;
        if (pthread_create(&tid, NULL, manejarCliente, nuevoFD) != 0) {
            perror("pthread_create");
            close(*nuevoFD);
            free(nuevoFD);
        } else {
            pthread_detach(tid);
        }
    }

    close(server_fd);
    return 0;
}
