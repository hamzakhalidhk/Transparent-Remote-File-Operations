#define _GNU_SOURCE

#include <dlfcn.h>
#include <stdio.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdarg.h>
#include <netinet/tcp.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <string.h>
#include <err.h>
#include <errno.h>

#define VERSION 1
#define FDBIAS 200

// RPC Header data structure
// RPC Header has a version number, a opcode (code),
// and the size of rpc_body (body_size). 
// Also, the RPC header can or cannot have flags.
typedef struct {
   unsigned int version;
   int code;
   int flags;
   int body_size;

} rpc_header;

// RPC Body data structure
// Each RPC has three fields in, inout, and out.
// We can choose to use these three fileds as per
// the data we want to send. RPC body can have an
// offset field. Finally, the RPC body has data
// field to send important data over the network.
typedef struct {
   int in;
   int inout;
   int out;
   off_t offset;
   char data[];
} rpc_body;

struct dirtreenode {
    char *name;
    int num_subdirs;
    struct dirtreenode **subdirs;
};

// Operation Codes
// ENUM of all the operation codes of the 
// implemented RPCs.
enum opcode {
    OPEN,
    WRITE,
    CLOSE,
    READ,
    LSEEK,
    STAT,
    UNLINK,
    GETDIRENTRIES,
    GETDIRTREE,
    FREEDIRTREE,
}; 

// Initializing variables
char *serverip;
char *serverport;
unsigned short port;
struct sockaddr_in srv;
int rv;
int sockfd = 0;

// Function declarations
int (*orig_open)(const char *pathname, int flags, ...); // mode_t mode is needed when flags includes O_CREAT
ssize_t (*orig_write)(int fildes, const void *buf, size_t nbyte);
int (*orig_close)(int fildes);
ssize_t (*orig_read)(int fd, void *buf, size_t count);
off_t (*orig_lseek)(int fd, off_t offset, int whence);
int (*orig_stat)(const char *path, struct stat *buf);
int (*orig_unlink)(const char *pathname);
ssize_t (*orig_getdirentries)(int fd, char *restrict buf, size_t nbytes, off_t *restrict basep);
struct dirtreenode *(*orig_getdirtree)(const char *path);
void (*orig_freedirtree)(struct dirtreenode *dirtree);
rpc_body* write_to_server(char *msg, size_t size, char* txt);

/**
 * @brief Opens a remote file descriptor
 *
 * The function generates an RPC header and body with information about the open operation to send to the server.
 * After creating the RPC message, the function calls write_to_server function. The function gets a reply from 
 * the server, modifies errno with the reply's inout value.
 * 
 * @param pathname the complete path of the file to open
 * @param flags file creation or file status flags
 * 
 * @return The file descriptor of a remote file
 */
int open(const char *pathname, int flags, ...)
{
    
    mode_t m = 0;
    if (flags & O_CREAT)
    {
        va_list a;
        va_start(a, flags);
        m = va_arg(a, mode_t);
        va_end(a);
    }

    int pathname_size = strlen(pathname) + 1;

    // Preparing header
    rpc_header* header = malloc(sizeof(rpc_header));
    header->version = VERSION;
    header->code = OPEN;
    header->flags = flags;
    header->body_size = pathname_size + sizeof(rpc_body);
    
    // Preparing body
    rpc_body* body = malloc(header->body_size);
    body->inout = m;
    strncpy(body->data, pathname, pathname_size);

    // Preparing message to send
    char *msg = malloc(header->body_size + sizeof(rpc_header));
    memcpy(msg, header, sizeof(rpc_header));
    memcpy(msg + sizeof(rpc_header), body, header->body_size);
    size_t size = header->body_size + sizeof(rpc_header);

    // Writing to server and getting the response
    rpc_body* response = write_to_server(msg, size, "OPEN");

    int file_descriptor = response->out;
    errno = response->inout;

    if (file_descriptor >= 0) {
        file_descriptor += FDBIAS;
    }

    // Freeing the memory
    free(msg);
    free(header);
    free(body);
    free(response);

    return file_descriptor;
}

/**
 * @brief Writes data into a buffer from a remote file descriptor
 *
 * The write operation is sent to orig_write if fd is smaller than FDBIAS.
 * A remote procedure call (RPC) is used to handle the write operation if fildes is equal to or greater than FDBIAS.
 *
 * The function generates an RPC header and body with information about the read operation to send to the server.
 * After creating the RPC message, the function calls write_to_server function. The function gets a reply from the
 * server, modifies errno with the reply's inout value, and outputs the number of bytes written.
 * 
 * @param fd file descriptor of a remote file to write to
 * @param buf buffer to write from
 * @param nbyte total number of bytes to write
 * 
 * @return The number of bytes written
 */
ssize_t write(int fildes, const void *buf, size_t nbyte) {

    if (fildes < FDBIAS) {
        return orig_write(fildes, buf, nbyte);
    }

    // Preparing header
    rpc_header* header = malloc(sizeof(rpc_header));
    header->version = VERSION;
    header->code = WRITE;
    header->body_size = sizeof(rpc_body) + nbyte + 1;

    rpc_body* body = malloc(header->body_size);
    body->in = fildes - FDBIAS;
    body->inout = nbyte;
    body->out = 0;

    memcpy(body->data, buf, nbyte);
    char *msg = malloc(header->body_size + sizeof(rpc_header));
    memcpy(msg, header, sizeof(rpc_header));
    memcpy(msg + sizeof(rpc_header), body, header->body_size);
    size_t size = header->body_size + sizeof(rpc_header);

    // Writing to server and getting the response
    rpc_body* response = write_to_server(msg, size, "WRITE");
    
    int file_descriptor = response->out;
    errno = response->inout;

    if (file_descriptor >= 0) {
        file_descriptor += FDBIAS;
    }

    free(msg);
    free(header);
    free(body);
    free(response);

    return file_descriptor;

}

/**
 * @brief Reads data from a remote file descriptor
 *
 * The read operation is sent to orig_read if fd is smaller than FDBIAS.
 * A remote procedure call (RPC) is used to handle the read operation if fildes is equal to or greater than FDBIAS.
 *
 * The function generates an RPC header and body with information about the read operation to send to the server.
 * After creating the RPC message, the function calls write_to_server function. The function gets a reply from the
 * server, modifies errno with the reply's inout value, and outputs the number of bytes read.
 * 
 * @param fd file descriptor of a remote file to read from
 * @param buf buffer to read into
 * @param nbyte total number of bytes to read
 * 
 * @return The number of bytes read
 */
ssize_t read(int fd, void *buf, size_t count)
{
    if (fd < FDBIAS) {
        return orig_read(fd, buf, count);
    }

    // Preparing header
    rpc_header* header = malloc(sizeof(rpc_header));
    header->version = VERSION;
    header->code = READ;
    header->body_size = sizeof(rpc_body);

    // Preparing body
    rpc_body* body = malloc(header->body_size);
    body->in = fd - FDBIAS;
    body->inout = count;
    body->out = 0;

    size_t msg_size = sizeof(rpc_header) + header->body_size;

    char *msg = malloc(msg_size);
    memcpy(msg, header, sizeof(rpc_header));
    memcpy(msg + sizeof(rpc_header), body, header->body_size);

    // Writing to server and getting the response
    rpc_body* response = write_to_server(msg, msg_size, "READ");

    int file_descriptor = response->out;
    errno = response->inout;
    
    memcpy(buf, (void *) response->data, count);

    free(header);
    free(response);
    free(body);
    free(msg);

    return file_descriptor;
}

/**
 * @brief Closes a remote file descriptor
 *
 * The close operation is sent to orig_close if fd is smaller than FDBIAS.
 * A remote procedure call (RPC) is used to handle the close operation if fildes is equal to or greater than FDBIAS.
 *
 * The function generates an RPC header and body with information about the close operation to send to the server.
 * After creating the RPC message, the function calls write_to_server function. The function gets a reply from the
 * server and returns zero on success. On error, -1 is returned, and errno is set to indicate the error.
 * 
 * @param fd file descriptor of a remote file to close
 * 
 * @return 0 on success and -1 on error
 */
int close(int fildes)
{
    if (fildes < FDBIAS) {
        return orig_close(fildes);
    }

    // Preparing header
    rpc_header* header = malloc(sizeof(rpc_header));
    header->version = VERSION;
    header->code = CLOSE;
    header->body_size = sizeof(rpc_body);

    // Preparing body
    rpc_body* body = malloc(header->body_size);
    body->in = fildes - FDBIAS;
    body->inout = 0;
    body->out = 0;

    size_t msg_size = sizeof(rpc_header) + header->body_size;
    char *msg = malloc(msg_size);
    memcpy(msg, header, sizeof(rpc_header));
    memcpy(msg + sizeof(rpc_header), body, header->body_size);

    rpc_body* response = write_to_server(msg, msg_size, "CLOSE");
    int file_descriptor = response->out;
    errno = response->inout;

    free(response);
    free(header);
    free(body);
    free(msg);

    return file_descriptor;
}

/**
* @brief Reposition the read/write file offset
* 
* This function repositions the read/write file offset of the open file with file descriptor fd to the given offset value, as per the value of whence.
* If the file descriptor fd is less than FDBIAS, it will call the original lseek function.
* 
* @param fd the file descriptor of the open file
* @param offset the offset to set
* @param whence specifies the reference position to set the offset.
* 
* @return the resulting file offset from the beginning of the file. On error, -1 is returned and errno is set.
**/
off_t lseek(int fd, off_t offset, int whence)
{
    if (fd < FDBIAS) {
        return orig_lseek(fd, offset, whence);
    }

    // Preparing header
    rpc_header* header = malloc(sizeof(rpc_header));
    header->version = VERSION;
    header->code = LSEEK;
    header->body_size = sizeof(rpc_body);

    // Preparing body
    rpc_body* body = malloc(header->body_size);
    body->in = fd - FDBIAS;
    body->inout = offset;
    body->out = whence;

    size_t msg_size = sizeof(rpc_header) + header->body_size;
    char *msg = malloc(msg_size);
    memcpy(msg, header, sizeof(rpc_header));
    memcpy(msg + sizeof(rpc_header), body, header->body_size);

    rpc_body* response = write_to_server(msg, msg_size, "LSEEK");
    int file_descriptor = response->out;
    errno = response->inout;

    free(response);
    free(header);
    free(body);
    free(msg);

    return file_descriptor;
}

/**
 * 
 * @file unlink.c
 * @brief Deletes a file or symbolic link.
 * 
 * Deletes the file or symbolic link specified in the pathname argument.
 * It returns 0 on success and -1 on failure.
 * 
 * @param pathname the pathname of the file or symbolic link to delete.
 * @return 0 on success and -1 on failure.
 **/
int unlink(const char *pathname)
{
    int pathname_size = strlen(pathname) + 1;

    // Preparing header
    rpc_header* header = malloc(sizeof(rpc_header));
    header->version = VERSION;
    header->code = UNLINK;
    header->flags = 0;
    header->body_size = pathname_size + sizeof(rpc_body);
    
    // Preparing body
    rpc_body* body = malloc(header->body_size);
    strncpy(body->data, pathname, pathname_size);

    // Preparing message to send
    char *msg = malloc(header->body_size + sizeof(rpc_header));
    memcpy(msg, header, sizeof(rpc_header));
    memcpy(msg + sizeof(rpc_header), body, header->body_size);
    size_t size = header->body_size + sizeof(rpc_header);

    // Writing to server and getting the response
    rpc_body* response = write_to_server(msg, size, "UNLINK");

    int res = response->out;
    errno = response->inout;

    free(response);
    free(header);
    free(body);
    free(msg);

    return res;
}

/**
 * @brief Retrieves information about the specified file and stores it in the buf.
 * 
 * @param path pathname of the file to retrieve information about
 * @param buf pointer to a struct stat structure to store the information
 * 
 * @return On success, a file descriptor. On error, -1.
 */
int stat(const char *path, struct stat *buf)
{
    int pathname_size = strlen(path) + 1;
    // Preparing header
    rpc_header* header = malloc(sizeof(rpc_header));
    header->version = VERSION;
    header->code = STAT;
    header->body_size = pathname_size + sizeof(rpc_body);
    
    // Preparing body
    rpc_body* body = malloc(header->body_size);
    //body->inout = pathname_size;
    memcpy(body->data, path, pathname_size);

    // Preparing message to send
    char *msg = malloc(header->body_size + sizeof(rpc_header));
    memcpy(msg, header, sizeof(rpc_header));
    memcpy(msg + sizeof(rpc_header), body, header->body_size);

    size_t size = header->body_size + sizeof(rpc_header);
    // Writing to server and getting the response
    rpc_body* response = write_to_server(msg, size, "STAT");

    int file_descriptor = response->out;
    errno = response->inout;
    memcpy(buf, response->data, sizeof(struct stat));

    free(response);
    free(header);
    free(body);
    free(msg);

    return file_descriptor;

}

/**
 *
 * @file getdirentries.c
 * @brief Retrieves directory entries from file descriptor
 * 
 * This function writes directory entries to the buffer after retrieving them from the
 * file descriptor. The amount of bytes that were written to the buffer is returned. 
 * 
 * @param fd file descriptor to retrieve directory entries from
 * @param buf buffer to write the directory entries to
 * @param nbytes number of bytes to write to the buffer
 * @param basep pointer to the offset in the directory
 * @return ssize_t number of bytes written to the buffer
 * 
 **/
ssize_t getdirentries(int fd, char *restrict buf, size_t nbytes, off_t *restrict basep)
{
    if (fd > FDBIAS) {
        fd = fd - FDBIAS;
    }
    
    // Preparing header
    rpc_header* header = malloc(sizeof(rpc_header));
    header->version = VERSION;
    header->code = GETDIRENTRIES;
    header->flags = 0;
    header->body_size = sizeof(rpc_body);
    
    rpc_body* body = malloc(header->body_size);
    body->in = fd;
    body->inout = nbytes;
    body->offset = *basep;

    // Preparing message to send
    char *msg = malloc(header->body_size + sizeof(rpc_header));
    memcpy(msg, header, sizeof(rpc_header));
    memcpy(msg + sizeof(rpc_header), body, header->body_size);

    size_t size = header->body_size + sizeof(rpc_header);
    // Writing to server and getting the response
    rpc_body* response = write_to_server(msg, size, "GETDIRENTRIES");
    int num = response->in;
    errno = response->inout;
    *basep = response->offset;

    memcpy(buf, response->data, nbytes);

    free(response);
    free(header);
    free(body);
    free(msg);

    return num;
}

/**
 * @brief Gets dir tree
 * 
 * @param path the path to generate dir tree from
 * 
 * @return dirtreenode, the root of the tree
 */
struct dirtreenode *getdirtree(const char *path)
{
    return orig_getdirtree(path);

}

/**
 * @brief Frees the dir tree
 * 
 * @param dirtreenode the root of the tree
 */
void freedirtree(struct dirtreenode *dirtree)
{
    orig_freedirtree(dirtree);
    return;
}

/**
 * 
 * @brief Sends a message to the server and receives a response.
 * 
 * Sends a message to the server and receives a response. The response is composed of a header and a body. 
 * The header contains information about the size of the body.
 * 
 * @param msg pointer to the message to be sent to the server.
 * @param size size of the message.
 * @param txt unused parameter.
 * 
 * @return Pointer to the response body.
**/
rpc_body* write_to_server(char *msg, size_t size, char* txt)
{    
    
    // send message to server
    ssize_t bytes_sent = send(sockfd, msg, size, 0); // send message; should check return value
    if (bytes_sent == -1) {
        printf("Error: %s\n", strerror(errno));
    }
    // Receiving response header
    rpc_header* response_header = malloc(sizeof(rpc_header));
    int bytes_recieved = 0;
    while (bytes_recieved < sizeof(rpc_header)) {
        rv = recv(sockfd, (char*)response_header + bytes_recieved, sizeof(rpc_header) - bytes_recieved, 0);
        if (rv <= 0) {
            printf("Corrupted Response Header");
            printf("recv failed : %s\n",strerror(errno));
            exit(0);
        }
        bytes_recieved += rv;

    }

    // Receiving response body
    rpc_body* response_body = malloc(response_header->body_size);
    bytes_recieved = 0;
    while (bytes_recieved < response_header->body_size) { 
        rv = recv(sockfd, ((char *) response_body) + bytes_recieved, response_header->body_size - bytes_recieved, 0);
        if (rv <= 0) {
            printf("Corrupted Response Body");
        }
        bytes_recieved += rv;
    }

    free(response_header);

    return response_body;
}

// This function is automatically called when program is started
void _init(void)
{
    // set function pointer orig_open to point to the original open function
    orig_open = dlsym(RTLD_NEXT, "open");
    orig_close = dlsym(RTLD_NEXT, "close");
    orig_read = dlsym(RTLD_NEXT, "read");
    orig_write = dlsym(RTLD_NEXT, "write");
    orig_lseek = dlsym(RTLD_NEXT, "lseek");
    orig_stat = dlsym(RTLD_NEXT, "stat");
    orig_unlink = dlsym(RTLD_NEXT, "unlink");
    orig_getdirentries = dlsym(RTLD_NEXT, "getdirentries");
    orig_getdirtree = dlsym(RTLD_NEXT, "getdirtree");
    orig_freedirtree = dlsym(RTLD_NEXT, "freedirtree");

    // Get environment variable indicating the ip address of the server
    serverip = getenv("server15440");
    if (!serverip)
    {
        serverip = "127.0.0.1";
    }

    // Get environment variable indicating the port of the server
    serverport = getenv("serverport15440");
    if (!serverport)
    {
        serverport = "15213";
    }
    
    port = (unsigned short)atoi(serverport);

    // Create socket
    sockfd = socket(AF_INET, SOCK_STREAM, 0); // TCP/IP socket

    if (sockfd < 0)
        err(1, 0); // in case of error

    // setup address structure to point to server
    memset(&srv, 0, sizeof(srv));              // clear it first
    srv.sin_family = AF_INET;                  // IP family
    srv.sin_addr.s_addr = inet_addr(serverip); // IP address of server
    srv.sin_port = htons(port);                // server port

    // actually connect to the server
    rv = connect(sockfd, (struct sockaddr *)&srv, sizeof(struct sockaddr));
    if (rv < 0)
        err(1, 0);

    // Turning off Nagle's Algorithm
    int flag = 1;

    if (setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag)) == -1) {
        // handle the error
        printf("Can't turn off Nagle's Algorithm!\n");
    }

}

void _fini(void) {
    orig_close(sockfd);
}
