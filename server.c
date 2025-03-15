/********************************************************
 * server.c
 *
 * Extensión del servidor con varias acciones del protocolo:
 *  - REGISTRO (ya teníamos)
 *  - EXIT
 *  - BROADCAST
 *  - DM
 *  - LISTA
 *  - MOSTRAR
 *  - ESTADO
 *
 * REFERENCIAS PDFs:
 *  - "Definición de proyecto Chat 2025, v1.pdf":
 *    -> Broadcasting, mensajes directos, listado de usuarios,
 *       salida controlada (EXIT), manejo de status, etc.
 *  - "Organización general.pdf":
 *    -> cJSON y campos del protocolo ("accion":"BROADCAST", etc.).
 *
 * COMPILAR:
 *   gcc client.c -o client -lcjson
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

#define PORT 50213
#define BACKLOG 10
#define BUFSIZE 1024
#define MAX_CLIENTS 10

typedef struct {
    int socketFD;
    char nombre[50];
    char ip[50];
    char status[10];  // "ACTIVO", "OCUPADO", "INACTIVO" etc.
    int activo;       // 1 si está conectado
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
 * Función auxiliar: buscar índice del cliente por FD
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

/********************************************************
 * Función auxiliar: liberarCliente (para "EXIT")
 ********************************************************/
void liberarCliente(int fd) {
    pthread_mutex_lock(&clientesMutex);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clientesConectados[i].activo == 1 &&
            clientesConectados[i].socketFD == fd) {
            // Marcarlo como inactivo
            clientesConectados[i].activo = 0;
            printf("[Servidor] Liberado cliente '%s' (FD=%d)\n",
                   clientesConectados[i].nombre, fd);
            break;
        }
    }
    pthread_mutex_unlock(&clientesMutex);
}

/********************************************************
 * BROADCAST: Enviar a todos el mensaje
 * Se espera un JSON del cliente:
 * {
 *   "accion": "BROADCAST",
 *   "nombre_emisor": "nombre_emisor",
 *   "mensaje": "texto del mensaje"
 * }
 ********************************************************/
void manejarBroadcast(int emisorFD, cJSON *root) {
    cJSON *nom = cJSON_GetObjectItem(root, "nombre_emisor");
    cJSON *msg = cJSON_GetObjectItem(root, "mensaje");
    if (!cJSON_IsString(nom) || !cJSON_IsString(msg)) {
        responderError(emisorFD, "FORMATO_BROADCAST_INVALIDO");
        return;
    }
    // Creamos el JSON que enviaremos a todos
    cJSON *bcast = cJSON_CreateObject();
    cJSON_AddStringToObject(bcast, "accion", "BROADCAST");
    cJSON_AddStringToObject(bcast, "nombre_emisor", nom->valuestring);
    cJSON_AddStringToObject(bcast, "mensaje", msg->valuestring);

    // Recorremos lista de clientes y enviamos
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
 * DM: Mensaje directo a un usuario
 * El PDF: "accion":"DM", "nombre_emisor":"...", "nombre_destinatario":"...", "mensaje":"..."
 ********************************************************/
void manejarDM(int emisorFD, cJSON *root) {
    cJSON *nomEmisor = cJSON_GetObjectItem(root, "nombre_emisor");
    cJSON *nomDest = cJSON_GetObjectItem(root, "nombre_destinatario");
    cJSON *msg = cJSON_GetObjectItem(root, "mensaje");

    if (!cJSON_IsString(nomEmisor) || !cJSON_IsString(nomDest) || !cJSON_IsString(msg)) {
        responderError(emisorFD, "FORMATO_DM_INVALIDO");
        return;
    }

    // Construimos el JSON a enviar al destinatario
    cJSON *dm = cJSON_CreateObject();
    cJSON_AddStringToObject(dm, "accion", "DM");
    cJSON_AddStringToObject(dm, "nombre_emisor", nomEmisor->valuestring);
    cJSON_AddStringToObject(dm, "nombre_destinatario", nomDest->valuestring);
    cJSON_AddStringToObject(dm, "mensaje", msg->valuestring);

    // Buscar al destinatario en la lista (por nombre)
    pthread_mutex_lock(&clientesMutex);
    int encontrado = 0;
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clientesConectados[i].activo == 1 &&
            strcmp(clientesConectados[i].nombre, nomDest->valuestring) == 0) {
            // Enviarle el DM
            enviarJSON(clientesConectados[i].socketFD, dm);
            encontrado = 1;
            break;
        }
    }
    pthread_mutex_unlock(&clientesMutex);

    if (!encontrado) {
        responderError(emisorFD, "DESTINATARIO_NO_ENCONTRADO");
    } else {
        // Podemos responder OK al emisor
        responderOK(emisorFD);
    }
    cJSON_Delete(dm);
}

/********************************************************
 * LISTA: Servidor responde con usuarios conectados
 * "accion":"LISTA"
 * Respuesta: {"accion":"LISTA","usuarios":["user1","user2",...]}
 ********************************************************/
void manejarLista(int emisorFD) {
    cJSON *resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "accion", "LISTA");

    cJSON *arrUsuarios = cJSON_CreateArray();

    pthread_mutex_lock(&clientesMutex);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clientesConectados[i].activo == 1) {
            cJSON_AddItemToArray(arrUsuarios, cJSON_CreateString(clientesConectados[i].nombre));
        }
    }
    pthread_mutex_unlock(&clientesMutex);

    cJSON_AddItemToObject(resp, "usuarios", arrUsuarios);

    enviarJSON(emisorFD, resp);
    cJSON_Delete(resp);
}

/********************************************************
 * MOSTRAR: info de un usuario (pdf sugiere IP o estado)
 * "tipo":"MOSTRAR","usuario":"..."  // en PDF
 * Respuesta: {"tipo":"MOSTRAR","usuario":"X","estado":"..." } etc.
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
            // Agregar info
            cJSON_AddStringToObject(resp, "usuario", clientesConectados[i].nombre);
            cJSON_AddStringToObject(resp, "estado", clientesConectados[i].status);
            // Si quieres también la IP:
            // cJSON_AddStringToObject(resp, "ip", clientesConectados[i].ip);
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
 * ESTADO: "tipo":"ESTADO","usuario":"X","estado":"ACTIVO"
 * Cambiar en la lista
 ********************************************************/
void manejarEstado(int emisorFD, cJSON *root) {
    cJSON *usuario = cJSON_GetObjectItem(root, "usuario");
    cJSON *estado = cJSON_GetObjectItem(root, "estado");
    if (!cJSON_IsString(usuario) || !cJSON_IsString(estado)) {
        responderError(emisorFD, "FORMATO_ESTADO_INVALIDO");
        return;
    }

    // Buscar en lista y modificar
    pthread_mutex_lock(&clientesMutex);
    int actualizado = 0;
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clientesConectados[i].activo == 1 &&
            strcmp(clientesConectados[i].nombre, usuario->valuestring) == 0) {
            strcpy(clientesConectados[i].status, estado->valuestring);
            actualizado = 1;
            break;
        }
    }
    pthread_mutex_unlock(&clientesMutex);

    if (!actualizado) {
        responderError(emisorFD, "USUARIO_NO_ENCONTRADO");
    } else {
        responderOK(emisorFD);
    }
}

/********************************************************
 * Manejo del hilo para cada cliente (similar a antes),
 * pero ahora manejamos las acciones extras:
 *   BROADCAST, DM, LISTA, EXIT, MOSTRAR, ESTADO
 ********************************************************/
void* manejarCliente(void *arg) {
    int clientFD = *(int*)arg;
    free(arg);

    while (1) {
        char buffer[BUFSIZE];
        memset(buffer, 0, BUFSIZE);
        int bytes = recv(clientFD, buffer, BUFSIZE - 1, 0);
        if (bytes <= 0) {
            printf("[Hilo] Cliente FD=%d desconectado.\n", clientFD);
            close(clientFD);
            // Liberar de la lista
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
        cJSON *tipo = cJSON_GetObjectItem(root, "tipo");
        
        // Recordatorio: "REGISTRO" y "EXIT" estaban en "tipo",
        // mientras BROADCAST, DM, LISTA están en "accion"
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
        else if (tipo && cJSON_IsString(tipo)) {
            if (strcmp(tipo->valuestring, "EXIT") == 0) {
                // Liberar y cerrar
                responderOK(clientFD);
                close(clientFD);
                liberarCliente(clientFD);
                cJSON_Delete(root);
                pthread_exit(NULL);
            } else if (strcmp(tipo->valuestring, "MOSTRAR") == 0) {
                manejarMostrar(clientFD, root);
            } else if (strcmp(tipo->valuestring, "ESTADO") == 0) {
                manejarEstado(clientFD, root);
            } else {
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
 * main() - Igual que antes, con lazos de accept + pthread
 ********************************************************/
int main() {
    // Inicializar la lista
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

    printf("[Servidor] Escuchando en puerto %d...\n", PORT);

    while (1) {
        int *nuevoFD = malloc(sizeof(int));
        *nuevoFD = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
        if (*nuevoFD < 0) {
            perror("accept");
            free(nuevoFD);
            continue;
        }
        // Podrías aquí llamar a tu rutina de REGISTRO si no lo haces en el hilo.
        // Pero lo normal es que sea el hilo quien lo maneje.

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
