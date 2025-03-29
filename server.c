/********************************************************
 * server.c
 * Servidor multihilo con desconexión por inactividad
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
#include <ctype.h>
#include <time.h>

#define PORT 50213
#define BACKLOG 10
#define BUFSIZE 1024
#define MAX_CLIENTS 10
#define TIEMPO_INACTIVIDAD 60    // 60 segundos de inactividad
#define INTERVALO_VERIFICACION 10 // Verificar cada 10 segundos

void strToUpper(char *dest, const char *src) {
    while (*src) {
        *dest = toupper((unsigned char)*src);
        dest++;
        src++;
    }
    *dest = '\0';
}

typedef struct {
    int socketFD;
    char nombre[50];
    char ip[50];
    char status[10];
    time_t ultimaActividad;
    int activo;
} Cliente;

static Cliente clientesConectados[MAX_CLIENTS];
static pthread_mutex_t clientesMutex = PTHREAD_MUTEX_INITIALIZER;

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

void enviarJSON(int socketFD, cJSON *obj) {
    char *str = cJSON_Print(obj);
    send(socketFD, str, strlen(str), 0);
    free(str);
}

int registrarUsuario(const char *nombre, const char *ip, int socketFD) {
    pthread_mutex_lock(&clientesMutex);

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clientesConectados[i].activo == 1) {
            if (strcmp(clientesConectados[i].nombre, nombre) == 0) {
                pthread_mutex_unlock(&clientesMutex);
                return -1;
            }
        }
    }

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clientesConectados[i].activo == 0) {
            clientesConectados[i].socketFD = socketFD;
            strcpy(clientesConectados[i].nombre, nombre);
            strcpy(clientesConectados[i].ip, ip);
            strcpy(clientesConectados[i].status, "ACTIVO");
            clientesConectados[i].ultimaActividad = time(NULL);
            clientesConectados[i].activo = 1;

            printf("[SERVIDOR] Usuario registrado: %s | IP: %s | FD: %d\n",
                clientesConectados[i].nombre, clientesConectados[i].ip, socketFD);
            pthread_mutex_unlock(&clientesMutex);
            return 0;
        }
    }

    pthread_mutex_unlock(&clientesMutex);
    return -1;
}

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

void manejarBroadcast(int emisorFD, cJSON *root) {
    cJSON *nom = cJSON_GetObjectItem(root, "nombre_emisor");
    cJSON *msg = cJSON_GetObjectItem(root, "mensaje");
    if (!cJSON_IsString(nom) || !cJSON_IsString(msg)) {
        responderError(emisorFD, "FORMATO_BROADCAST_INVALIDO");
        return;
    }

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
    char *strJson = cJSON_Print(resp);
    send(emisorFD, strJson, strlen(strJson), 0);
    free(strJson);
    cJSON_Delete(resp);
}

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
            cJSON_AddStringToObject(resp, "User", clientesConectados[i].nombre);
            cJSON_AddStringToObject(resp, "estado", clientesConectados[i].status);
            cJSON_AddStringToObject(resp, "IP", clientesConectados[i].ip);
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

void manejarEstado(int emisorFD, cJSON *root) {
    cJSON *usuario = cJSON_GetObjectItem(root, "usuario");
    cJSON *estado  = cJSON_GetObjectItem(root, "estado");

    if (!cJSON_IsString(usuario) || !cJSON_IsString(estado)) {
        responderError(emisorFD, "FORMATO_ESTADO_INVALIDO");
        return;
    }

    char nuevoEstado[20];
    strToUpper(nuevoEstado, estado->valuestring);

           // Verificar que sea uno de los tres permitidos
       if (strcmp(nuevoEstado, "ACTIVO") != 0 &&
       strcmp(nuevoEstado, "OCUPADO") != 0 &&
       strcmp(nuevoEstado, "INACTIVO") != 0) {
       responderError(emisorFD, "ESTADO_INVALIDO");
       return;
       }

    pthread_mutex_lock(&clientesMutex);
    int encontrado = 0;
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clientesConectados[i].activo == 1 &&
            strcmp(clientesConectados[i].nombre, usuario->valuestring) == 0) {

            char estadoActual[20];
            strToUpper(estadoActual, clientesConectados[i].status);

            if (strcmp(estadoActual, nuevoEstado) == 0) {
                pthread_mutex_unlock(&clientesMutex);
                responderError(emisorFD, "ESTADO_YA_SELECCIONADO");
                return;
            }

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
* Función de verificación de inactividad (modificada)
********************************************************/
void* verificarInactividad(void *arg) {
   while (1) {
       sleep(INTERVALO_VERIFICACION);
       time_t ahora = time(NULL);

       pthread_mutex_lock(&clientesMutex);
       for (int i = 0; i < MAX_CLIENTS; i++) {
           if (clientesConectados[i].activo == 1) {
               double segundosInactivo = difftime(ahora, clientesConectados[i].ultimaActividad);
               if (segundosInactivo >= TIEMPO_INACTIVIDAD) {
                   printf("[Servidor] Usuario %s marcado como INACTIVO (%.0f segundos)\n",
                          clientesConectados[i].nombre, segundosInactivo);
                   strcpy(clientesConectados[i].status, "INACTIVO");  // Solo cambia el estado
               }
           }
       }
       pthread_mutex_unlock(&clientesMutex);
   }
   return NULL;
}

void* manejarCliente(void *arg) {
    int clientFD = *(int*)arg;
    free(arg);

    while (1) {
        char buffer[BUFSIZE];
        memset(buffer, 0, BUFSIZE);
        int bytes = recv(clientFD, buffer, BUFSIZE - 1, 0);
        if (bytes <= 0) {
            printf("[Hilo] Cliente FD: %d desconectado\n", clientFD);
            close(clientFD);
            liberarCliente(clientFD);
            pthread_exit(NULL);
        }

        // Actualizar actividad y estado
       pthread_mutex_lock(&clientesMutex);
       for (int i = 0; i < MAX_CLIENTS; i++) {
           if (clientesConectados[i].activo == 1 && 
               clientesConectados[i].socketFD == clientFD) {
               clientesConectados[i].ultimaActividad = time(NULL);
               strcpy(clientesConectados[i].status, "ACTIVO");  // Resetear a ACTIVO
               break;
           }
       }
       pthread_mutex_unlock(&clientesMutex);


        cJSON *root = cJSON_Parse(buffer);
        if (!root) {
            responderError(clientFD, "JSON_INVALIDO");
            continue;
        }

        cJSON *accion = cJSON_GetObjectItem(root, "accion");
        cJSON *tipo   = cJSON_GetObjectItem(root, "tipo");

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
            if (strcmp(tipo->valuestring, "REGISTRO") == 0) {
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

int main() {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        clientesConectados[i].activo = 0;
        strcpy(clientesConectados[i].status, "ACTIVO");
    }

    // Iniciar hilo de verificación de inactividad
    pthread_t hilo_verificador;
    if (pthread_create(&hilo_verificador, NULL, verificarInactividad, NULL) != 0) {
        perror("Error al crear hilo de verificacion");
        exit(EXIT_FAILURE);
    }
    pthread_detach(hilo_verificador);

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

    while (1) {  // <--- Bucle principal de aceptación
       int *nuevoFD = malloc(sizeof(int));
       *nuevoFD = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
       if (*nuevoFD < 0) {
           perror("accept");
           free(nuevoFD);
           continue;
       }

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
}  // <--- Cierre de la función main()