/********************************************************
 * servidor_chat.c - Versión sin uso de IP
 * - Compatible con Linux y Windows
 * - Implementa: REGISTRO, EXIT, BROADCAST, DM, LISTA,
 *               MOSTRAR, ESTADO (solo ACTIVO, OCUPADO, INACTIVO)
 * - Desconecta al cliente si manda campos de REGISTRO inválidos.
 *
 * Referencias:
 *   - Definición de proyecto Chat 2025, v1.pdf
 *   - Organización general.pdf
 *
 * Compilación:
 *   Linux:   gcc servidor_chat.c -o servidor -lpthread -lcjson
 *   Windows: usar compilador que soporte WinSock + cJSON
 ********************************************************/

 #ifdef _WIN32
 #include <winsock2.h>
 #include <ws2tcpip.h>
 #include <windows.h>
 #pragma comment(lib, "ws2_32.lib")
#else
 #include <arpa/inet.h>
 #include <unistd.h>
 #include <netinet/in.h>
 #include <sys/socket.h>
 #include <pthread.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h> // <--- NUEVO: Para controlar tiempo (time_t, time, difftime)
#include "cJSON.h"

#define PORT 50213
#define BACKLOG 10
#define BUFSIZE 1024
#define MAX_CLIENTS 10

/********************************************************
* Estructura que guarda info de cada cliente conectado
* (Eliminamos la IP, ya no es necesaria)
********************************************************/
typedef struct {
 int socketFD;
 char nombre[50];
 char status[10];  // ACTIVO, OCUPADO, INACTIVO
 int activo;       // 1 conectado, 0 no
 time_t ultimaActividad; // <--- NUEVO: para controlar inactividad
} Cliente;

/** Arreglo global de clientes */
static Cliente clientesConectados[MAX_CLIENTS];

/** Mutex para acceso concurrente (Windows / Linux) */
#ifdef _WIN32
 static HANDLE clientesMutex;
#else
 static pthread_mutex_t clientesMutex = PTHREAD_MUTEX_INITIALIZER;
#endif

void lock_mutex() {
#ifdef _WIN32
 WaitForSingleObject(clientesMutex, INFINITE);
#else
 pthread_mutex_lock(&clientesMutex);
#endif
}

void unlock_mutex() {
#ifdef _WIN32
 ReleaseMutex(clientesMutex);
#else
 pthread_mutex_unlock(&clientesMutex);
#endif
}

/** Convierte src a mayúsculas y pone el resultado en dest */
void strToUpper(char *dest, const char *src) {
 while (*src) {
     *dest = (char)toupper((unsigned char)*src);
     dest++;
     src++;
 }
 *dest = '\0';
}

/********************************************************
* Respuestas en JSON
********************************************************/
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

/** Envía un cJSON cualquiera al socket */
void enviarJSON(int socketFD, cJSON *obj) {
 char *str = cJSON_Print(obj);
 send(socketFD, str, strlen(str), 0);
 free(str);
}

/********************************************************
* Registrar usuario
*  - Ya no recibe IP.
*  - Si nombre está duplicado o no hay espacio, retorna -1
********************************************************/
int registrarUsuario(const char *nombre, int socketFD) {
 lock_mutex();

 // Verificar si nombre ya está ocupado
 for (int i = 0; i < MAX_CLIENTS; i++) {
     if (clientesConectados[i].activo == 1 &&
         strcmp(clientesConectados[i].nombre, nombre) == 0) {
         unlock_mutex();
         return -1; // nombre duplicado
     }
 }
 // Buscar espacio libre
 for (int i = 0; i < MAX_CLIENTS; i++) {
     if (!clientesConectados[i].activo) {
         clientesConectados[i].socketFD = socketFD;
         strcpy(clientesConectados[i].nombre, nombre);
         // Al registrar, lo dejamos en ACTIVO por defecto
         strcpy(clientesConectados[i].status, "ACTIVO");
         clientesConectados[i].activo = 1;
         clientesConectados[i].ultimaActividad = time(NULL); // <--- NUEVO

         printf("[SERVIDOR] Usuario registrado: %s | FD: %d\n",
                nombre, socketFD);

         unlock_mutex();
         return 0;
     }
 }

 unlock_mutex();
 return -1; // sin espacio
}

/********************************************************
* Liberar un cliente
********************************************************/
void liberarCliente(int fd) {
 lock_mutex();
 for (int i = 0; i < MAX_CLIENTS; i++) {
     if (clientesConectados[i].activo &&
         clientesConectados[i].socketFD == fd) {
         clientesConectados[i].activo = 0;
         printf("[SERVIDOR] Cliente '%s' liberado (FD:%d)\n",
                clientesConectados[i].nombre, fd);
         break;
     }
 }
 unlock_mutex();
}

/********************************************************
* Manejar BROADCAST
********************************************************/
void manejarBroadcast(int emisorFD, cJSON *root) {
 cJSON *nom = cJSON_GetObjectItem(root, "nombre_emisor");
 cJSON *msg = cJSON_GetObjectItem(root, "mensaje");

 if (!cJSON_IsString(nom) || !cJSON_IsString(msg)) {
     responderError(emisorFD, "FORMATO_BROADCAST_INVALIDO");
     return;
 }

 // Preparamos el JSON de Broadcast
 cJSON *bcast = cJSON_CreateObject();
 cJSON_AddStringToObject(bcast, "accion", "BROADCAST");
 cJSON_AddStringToObject(bcast, "nombre_emisor", nom->valuestring);
 cJSON_AddStringToObject(bcast, "mensaje", msg->valuestring);

 printf("[SERVIDOR] BROADCAST de '%s': %s\n", nom->valuestring, msg->valuestring);

 lock_mutex();
 for (int i = 0; i < MAX_CLIENTS; i++) {
     if (clientesConectados[i].activo) {
         enviarJSON(clientesConectados[i].socketFD, bcast);
     }
 }
 unlock_mutex();

 cJSON_Delete(bcast);
}

/********************************************************
* Manejar DM
********************************************************/
void manejarDM(int emisorFD, cJSON *root) {
 cJSON *nomEmisor = cJSON_GetObjectItem(root, "nombre_emisor");
 cJSON *nomDest   = cJSON_GetObjectItem(root, "nombre_destinatario");
 cJSON *msg       = cJSON_GetObjectItem(root, "mensaje");

 if (!cJSON_IsString(nomEmisor) || !cJSON_IsString(nomDest) || !cJSON_IsString(msg)) {
     responderError(emisorFD, "FORMATO_DM_INVALIDO");
     return;
 }

 // Armar JSON de DM
 cJSON *dm = cJSON_CreateObject();
 cJSON_AddStringToObject(dm, "accion", "DM");
 cJSON_AddStringToObject(dm, "nombre_emisor", nomEmisor->valuestring);
 cJSON_AddStringToObject(dm, "nombre_destinatario", nomDest->valuestring);
 cJSON_AddStringToObject(dm, "mensaje", msg->valuestring);

 printf("[SERVIDOR] DM de '%s' para '%s': %s\n",
        nomEmisor->valuestring, nomDest->valuestring, msg->valuestring);

 int encontrado = 0;
 lock_mutex();
 for (int i = 0; i < MAX_CLIENTS; i++) {
     if (clientesConectados[i].activo &&
         strcmp(clientesConectados[i].nombre, nomDest->valuestring) == 0) {
         enviarJSON(clientesConectados[i].socketFD, dm);
         encontrado = 1;
         break;
     }
 }
 unlock_mutex();

 if (!encontrado) {
     responderError(emisorFD, "DESTINATARIO_NO_ENCONTRADO");
 } else {
     responderOK(emisorFD);
 }

 cJSON_Delete(dm);
}

/********************************************************
* Manejar LISTA
********************************************************/
void manejarLista(int emisorFD) {
 cJSON *resp = cJSON_CreateObject();
 cJSON_AddStringToObject(resp, "accion", "LISTA");
 cJSON *usuarios = cJSON_CreateArray();

 lock_mutex();
 printf("[SERVIDOR] Preparando lista de usuarios...\n");
 for (int i = 0; i < MAX_CLIENTS; i++) {
     if (clientesConectados[i].activo) {
         cJSON_AddItemToArray(usuarios, cJSON_CreateString(clientesConectados[i].nombre));
     }
 }
 unlock_mutex();

 cJSON_AddItemToObject(resp, "usuarios", usuarios);
 enviarJSON(emisorFD, resp);
 cJSON_Delete(resp);
}

/********************************************************
* Manejar MOSTRAR
********************************************************/
void manejarMostrar(int emisorFD, cJSON *root) {
 cJSON *usuario = cJSON_GetObjectItem(root, "usuario");
 if (!cJSON_IsString(usuario)) {
     responderError(emisorFD, "FORMATO_MOSTRAR_INVALIDO");
     return;
 }

 cJSON *resp = cJSON_CreateObject();
 cJSON_AddStringToObject(resp, "tipo", "MOSTRAR");

 int encontrado = 0;
 lock_mutex();
 for (int i = 0; i < MAX_CLIENTS; i++) {
     if (clientesConectados[i].activo &&
         strcmp(clientesConectados[i].nombre, usuario->valuestring) == 0) {
         cJSON_AddStringToObject(resp, "usuario", clientesConectados[i].nombre);
         cJSON_AddStringToObject(resp, "estado", clientesConectados[i].status);
         encontrado = 1;
         break;
     }
 }
 unlock_mutex();

 if (!encontrado) {
     responderError(emisorFD, "USUARIO_NO_ENCONTRADO");
 } else {
     enviarJSON(emisorFD, resp);
 }
 cJSON_Delete(resp);
}

/********************************************************
* Manejar ESTADO (ACTIVO, OCUPADO, INACTIVO)
********************************************************/
void manejarEstado(int emisorFD, cJSON *root) {
 cJSON *usuario = cJSON_GetObjectItem(root, "usuario");
 cJSON *estado  = cJSON_GetObjectItem(root, "estado");
 if (!cJSON_IsString(usuario) || !cJSON_IsString(estado)) {
     responderError(emisorFD, "FORMATO_ESTADO_INVALIDO");
     return;
 }

 // Pasar el estado solicitado a mayúsculas para comparar
 char nuevoEstado[20];
 strToUpper(nuevoEstado, estado->valuestring);

 // Verificar que sea uno de los tres permitidos
 if (strcmp(nuevoEstado, "ACTIVO") != 0 &&
     strcmp(nuevoEstado, "OCUPADO") != 0 &&
     strcmp(nuevoEstado, "INACTIVO") != 0) {
     responderError(emisorFD, "ESTADO_INVALIDO");
     return;
 }

 int encontrado = 0;
 lock_mutex();
 for (int i = 0; i < MAX_CLIENTS; i++) {
     if (clientesConectados[i].activo &&
         strcmp(clientesConectados[i].nombre, usuario->valuestring) == 0) {

         // Si ya lo tenía
         if (strcmp(clientesConectados[i].status, nuevoEstado) == 0) {
             unlock_mutex();
             responderError(emisorFD, "ESTADO_YA_SELECCIONADO");
             return;
         }

         // Actualizamos y respondemos
         strcpy(clientesConectados[i].status, nuevoEstado);
         printf("[SERVIDOR] Cliente '%s' cambió estado a '%s'\n",
                clientesConectados[i].nombre, nuevoEstado);

         encontrado = 1;
         break;
     }
 }
 unlock_mutex();

 if (!encontrado) {
     responderError(emisorFD, "USUARIO_NO_ENCONTRADO");
 } else {
     responderOK(emisorFD);
 }
}

/********************************************************
* Hilo para manejar a cada cliente
********************************************************/
void* manejarCliente(void *arg) {
 int clientFD = *(int*)arg;
 free(arg);

 while (1) {
     char buffer[BUFSIZE];
     memset(buffer, 0, BUFSIZE);

     int bytes = recv(clientFD, buffer, BUFSIZE - 1, 0);
     if (bytes <= 0) {
         printf("[SERVIDOR] Cliente FD:%d desconectado\n", clientFD);
#ifdef _WIN32
         closesocket(clientFD);
#else
         close(clientFD);
#endif
         liberarCliente(clientFD);
         return 0;
     }

     printf("[SERVIDOR] Mensaje recibido (FD:%d): %s\n", clientFD, buffer);

     // <--- NUEVO: actualizar ultimaActividad al recibir cualquier msg
     lock_mutex();
     for (int i = 0; i < MAX_CLIENTS; i++) {
         if (clientesConectados[i].activo &&
             clientesConectados[i].socketFD == clientFD) {
             clientesConectados[i].ultimaActividad = time(NULL);
             break;
         }
     }
     unlock_mutex();

     cJSON *root = cJSON_Parse(buffer);
     if (!root) {
         responderError(clientFD, "JSON_INVALIDO");
#ifdef _WIN32
         closesocket(clientFD);
#else
         close(clientFD);
#endif
         liberarCliente(clientFD);
         return 0;
     }

     // Revisamos "accion" o "tipo"
     cJSON *accion = cJSON_GetObjectItem(root, "accion");
     cJSON *tipo   = cJSON_GetObjectItem(root, "tipo");

     if (accion && cJSON_IsString(accion)) {
         // BROADCAST, DM, LISTA
         if (strcmp(accion->valuestring, "BROADCAST") == 0) {
             manejarBroadcast(clientFD, root);
         } else if (strcmp(accion->valuestring, "DM") == 0) {
             manejarDM(clientFD, root);
         } else if (strcmp(accion->valuestring, "LISTA") == 0) {
             manejarLista(clientFD);
         } else {
             responderError(clientFD, "ACCION_NO_IMPLEMENTADA");
         }
     } else if (tipo && cJSON_IsString(tipo)) {
         // REGISTRO, EXIT, MOSTRAR, ESTADO
         if (strcmp(tipo->valuestring, "REGISTRO") == 0) {
             cJSON *usuario = cJSON_GetObjectItem(root, "usuario");

             if (!cJSON_IsString(usuario)) {
                 responderError(clientFD, "CAMPOS_REGISTRO_INVALIDOS");
#ifdef _WIN32
                 closesocket(clientFD);
#else
                 close(clientFD);
#endif
                 liberarCliente(clientFD);
                 cJSON_Delete(root);
                 return 0;
             } else {
                 if (registrarUsuario(usuario->valuestring, clientFD) == 0) {
                     responderOK(clientFD);
                 } else {
                     responderError(clientFD, "USUARIO_DUPLICADO");
#ifdef _WIN32
                     closesocket(clientFD);
#else
                     close(clientFD);
#endif
                     liberarCliente(clientFD);
                     cJSON_Delete(root);
                     return 0;
                 }
             }
         }
         else if (strcmp(tipo->valuestring, "EXIT") == 0) {
             responderOK(clientFD);
             printf("[SERVIDOR] Cliente FD:%d solicitó salir\n", clientFD);
#ifdef _WIN32
             closesocket(clientFD);
#else
             close(clientFD);
#endif
             liberarCliente(clientFD);
             cJSON_Delete(root);
             return 0;
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
     } else {
         responderError(clientFD, "FALTA_TIPO_O_ACCION");
#ifdef _WIN32
         closesocket(clientFD);
#else
         close(clientFD);
#endif
         liberarCliente(clientFD);
         cJSON_Delete(root);
         return 0;
     }

     cJSON_Delete(root);
 }

 return 0;
}

/********************************************************
* monitorInactividad - Hilo para desconectar inactivos
********************************************************/
// <--- NUEVO: desconectar a quien lleve 1 minuto (60s) sin actividad
#ifdef _WIN32
DWORD WINAPI monitorInactividad(LPVOID arg)
#else
void* monitorInactividad(void* arg)
#endif
{
  while(1) {
    lock_mutex();
    time_t ahora = time(NULL);
    for(int i = 0; i < MAX_CLIENTS; i++){
      if (clientesConectados[i].activo) {
        double diff = difftime(ahora, clientesConectados[i].ultimaActividad);
        // *1 minuto de inactividad*
        if (diff > 60) {
          printf("[SERVIDOR] Desconectado por inactividad a '%s' (FD:%d)\n",
                 clientesConectados[i].nombre, clientesConectados[i].socketFD);
#ifdef _WIN32
          closesocket(clientesConectados[i].socketFD);
#else
          close(clientesConectados[i].socketFD);
#endif
          clientesConectados[i].activo = 0;
        }
      }
    }
    unlock_mutex();

    // Esperar 30s antes de checar otra vez, por ejemplo
#ifdef _WIN32
    Sleep(30000);
#else
    sleep(30);
#endif
  }
#ifndef _WIN32
  return NULL;
#endif
}

/********************************************************
* main()
********************************************************/
int main() {
#ifdef _WIN32
 // Inicializa WinSock
 WSADATA wsaData;
 WSAStartup(MAKEWORD(2, 2), &wsaData);
 // Crea el mutex
 clientesMutex = CreateMutex(NULL, FALSE, NULL);
#endif

 // Inicializar array de clientes
 for (int i = 0; i < MAX_CLIENTS; i++) {
     clientesConectados[i].activo = 0;
     strcpy(clientesConectados[i].status, "ACTIVO");
 }

 // Crear socket
 int server_fd = socket(AF_INET, SOCK_STREAM, 0);
 if (server_fd < 0) {
     perror("socket");
     exit(1);
 }

 // Configurar estructura server_addr
 struct sockaddr_in server_addr, client_addr;
 socklen_t client_len = sizeof(client_addr);
 memset(&server_addr, 0, sizeof(server_addr));

 server_addr.sin_family = AF_INET;
 server_addr.sin_port   = htons(PORT);
 server_addr.sin_addr.s_addr = INADDR_ANY;

 // Vincular
 if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
     perror("bind");
#ifdef _WIN32
     closesocket(server_fd);
     WSACleanup();
#else
     close(server_fd);
#endif
     exit(1);
 }

 // Escuchar
 if (listen(server_fd, BACKLOG) < 0) {
     perror("listen");
#ifdef _WIN32
     closesocket(server_fd);
     WSACleanup();
#else
     close(server_fd);
#endif
     exit(1);
 }

 printf("[SERVIDOR] Escuchando en puerto %d...\n", PORT);

 // <--- NUEVO: Crear hilo monitorInactividad
#ifdef _WIN32
  CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)monitorInactividad, NULL, 0, NULL);
#else
  pthread_t tidMonitor;
  pthread_create(&tidMonitor, NULL, monitorInactividad, NULL);
  pthread_detach(tidMonitor);
#endif

 // Aceptar clientes en bucle
 while (1) {
     int *nuevoFD = (int*)malloc(sizeof(int));
     *nuevoFD = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
     if (*nuevoFD < 0) {
         perror("accept");
         free(nuevoFD);
         continue;
     }

     printf("[SERVIDOR] Nueva conexión aceptada (FD:%d)\n", *nuevoFD);

#ifdef _WIN32
     // Windows: CreateThread
     CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)manejarCliente, nuevoFD, 0, NULL);
#else
     // Linux: pthread_create
     pthread_t tid;
     pthread_create(&tid, NULL, manejarCliente, nuevoFD);
     pthread_detach(tid);
#endif
 }

#ifdef _WIN32
 CloseHandle(clientesMutex);
 WSACleanup();
#endif

 return 0;
}
