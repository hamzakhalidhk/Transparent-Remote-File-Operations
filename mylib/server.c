#include <stdio.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <string.h>
#include <unistd.h>
#include <err.h>
#include <fcntl.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>

#define TREESIZE 100000
#define VERSION 1

typedef struct {
   
   unsigned int version;
   int code;
   int flags;
   int body_size;

} rpc_header;

typedef struct {
   
   int in;
   int inout;
   long int out;
   off_t offset;
   char data[];

} rpc_body;

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


struct dirtreenode {
	char *name;
	int num_subdirs;
	struct dirtreenode **subdirs;
};

struct dirtreenode *(*orig_getdirtree)(const char *path);

rpc_header* prepare_response_header(rpc_header* header, int opcode, int flags, int body_size) {
    // Preparing response header
    rpc_header* response_header = malloc(sizeof(rpc_header));
    response_header->version = header->version;
    response_header->code = opcode;
    response_header->flags = flags;
    response_header->body_size = body_size; 
    return response_header;
}

rpc_body* prepare_response_body(rpc_header* response_header, int in, int inout, int out, char* response_data) {
    // Preparing response header
    rpc_body* response_body = malloc(response_header->body_size);
    response_body->in = in;
    response_body->inout = inout;
    response_body->out = out;
    memcpy(response_body->data, response_data, strlen(response_data) + 1); 

    return response_body;

}

void write_to_client(int sockfd, 
                    rpc_header* response_header, 
                    rpc_body* response_body) {

    // Preparing response to send to the client
    char *response = malloc(response_header->body_size + sizeof(rpc_header));
    memcpy(response, response_header, sizeof(rpc_header));
    memcpy(response + sizeof(rpc_header), response_body, response_header->body_size);
    size_t response_size = response_header->body_size + sizeof(rpc_header);

    // Sending error response
    if (send(sockfd, response, response_size, 0) < 0) {
        // Freeing the memory
        free(response);
        free(response_header);
        free(response_body);
        return;
    }

    free(response);
    free(response_header);
    free(response_body);

} 

void rpc_open(int sockfd, rpc_header *header, rpc_body *body) {

    const char *pathname = body->data;

    int fd = open(body->data, header->flags, body->inout);

    char* response_data = "File opened successfully.";
    if (fd < 0) {
        response_data = "Error opening the file";
    }
    
    int body_size = strlen(response_data) + sizeof(rpc_body) + 1;

    rpc_header* response_header = prepare_response_header(header, OPEN, 0, body_size);
    rpc_body* response_body = prepare_response_body(response_header, 0, errno, fd, response_data);

    write_to_client(sockfd, response_header, response_body);

}


void rpc_read(int sockfd, rpc_header *header, rpc_body *body) {
    
    rpc_body* response_body = malloc(sizeof(rpc_body) + body->inout);
    int fd = read(body->in, (void *) response_body->data, body->inout);

    char* response_data = "File read successfully.";
    if (fd < 0) {
        if (errno == EBADF) {
            fprintf(stderr, "The file descriptor is not a valid file descriptor\n");
        } else if (errno == EACCES) {
            fprintf(stderr, "The file does not have the necessary permissions for reading\n");
        } else {
            fprintf(stderr, "An error occurred while reading the file: %s\n", strerror(errno));
        }
        response_data = "Error reading the file.";
    }

    response_body->inout = errno;
    response_body->out = fd;

    int body_size = sizeof(rpc_body) + body->inout;
    rpc_header* response_header = prepare_response_header(header, READ, 0, body_size);

    write_to_client(sockfd, response_header, response_body);


}

void rpc_close(int sockfd, rpc_header *header, rpc_body *body) {

    int fd = close(body->in);

    char* response_data = "File closed successfully.";
    if (fd < 0) {
        //perror("close");
        if (errno == EBADF) {
            fprintf(stderr, "The file descriptor is not a valid file descriptor\n");
        } else if (errno == EACCES) {
            fprintf(stderr, "The file does not have the necessary permissions for reading\n");
        } else {
            fprintf(stderr, "An error occurred while reading the file: %s\n", strerror(errno));
        }
        response_data = "Error closing the file.";
    }

    int body_size = strlen(response_data) + sizeof(rpc_body) + 1;

    rpc_header* response_header = prepare_response_header(header, CLOSE, 0, body_size);
    rpc_body* response_body = prepare_response_body(response_header, 0, errno, fd, response_data);

    write_to_client(sockfd, response_header, response_body);

}

void rpc_write(int sockfd, rpc_header *header, rpc_body *body) {

    int fd = write(body->in, body->data, body->inout);

    char* response_data = "File written successfully.";
    if (fd < 0) {
        if (errno == EBADF) {
            fprintf(stderr, "The file descriptor is not a valid file descriptor\n");
        } else if (errno == EACCES) {
            fprintf(stderr, "The file does not have the necessary permissions for reading\n");
        } else {
            fprintf(stderr, "An error occurred while reading the file: %s\n", strerror(errno));
        }
        response_data = "Error writing the file.";
    }

    int body_size = strlen(response_data) + sizeof(rpc_body) + 1;

    rpc_header* response_header = prepare_response_header(header, WRITE, 0, body_size);
    rpc_body* response_body = prepare_response_body(response_header, 0, errno, fd, response_data);

    write_to_client(sockfd, response_header, response_body);


}


void rpc_unlink(int sockfd, rpc_header *header, rpc_body *body) {

    int res = unlink(body->data);

    char* response_data = "File unlinked successfully.";
    if (res < 0) {
        response_data = "Error unlinking the file.";
    }

    int body_size = strlen(response_data) + sizeof(rpc_body) + 1;

    rpc_header* response_header = prepare_response_header(header, UNLINK, 0, body_size);
    rpc_body* response_body = prepare_response_body(response_header, 0, errno, res, response_data);

    write_to_client(sockfd, response_header, response_body);

}

void rpc_stat(int sockfd, rpc_header *header, rpc_body *body) {

    //int buf_size = header->body_size - sizeof(rpc_body) - body->inout;
    struct stat *buf = malloc(sizeof(struct stat));
    int res = stat(body->data, buf);
    
    char* response_data = "Stat successful.";
    if (res < 0) {
        response_data = "Error in stat.";
    }

    int body_size = sizeof(struct stat) + sizeof(rpc_body);

    rpc_header* response_header = prepare_response_header(header, STAT, 0, body_size);
    rpc_body* response_body = malloc(body_size);
    response_body->out = res;
    response_body->inout = errno;
    memcpy(response_body->data, buf, sizeof(struct stat));

    write_to_client(sockfd, response_header, response_body);

    free(buf);

}

void rpc_lseek(int sockfd, rpc_header *header, rpc_body *body) {
    
    int res = lseek(body->in, body->inout, body->out);

    char* response_data = "File repositioned successfully.";
    if (res < 0) {
        response_data = "Error repositioning the file.";

    }

    int body_size = strlen(response_data) + sizeof(rpc_body) + 1;

    rpc_header* response_header = prepare_response_header(header, LSEEK, 0, body_size);
    rpc_body* response_body = prepare_response_body(response_header, 0, errno, res, response_data);

    write_to_client(sockfd, response_header, response_body);

}

void rpc_getdirentries(int sockfd, rpc_header *header, rpc_body *body) {

    char* buf = malloc(body->inout);
    
    rpc_body* response_body = malloc(sizeof(rpc_body) + body->inout);
    off_t *restrict basep = &body->offset;

    int res = getdirentries(body->in, buf, (size_t) body->inout, basep);

    char* response_data = "Got directory entries successfully.";
    if (res < 0) {
        response_data = "Error getting directory entries";
    }

    int body_size = res + sizeof(rpc_body);

    rpc_header* response_header = prepare_response_header(header, GETDIRENTRIES, 0, body_size);
    response_body->in = res;
    response_body->inout = errno;
    response_body->offset = *basep;
    memcpy(response_body->data, buf, res);
    
    write_to_client(sockfd, response_header, response_body);

    free(buf);

}

void handle_connection(int sessfd) {

    rpc_header* header;
    rpc_body* body;

    while(1) {

        int rv;
        header = malloc(sizeof(rpc_header));
        // Receiving header
        int bytes_recieved = 0;
        while (bytes_recieved < sizeof(rpc_header)) {
            rv = recv(sessfd, (char*)header + bytes_recieved, sizeof(rpc_header) - bytes_recieved, 0);
            if (rv <= 0) {
                err(1, 0);
                break;
            }
            bytes_recieved += rv;
        }

        // Receiving body
        body = malloc(header->body_size);
        bytes_recieved = 0;
        while (bytes_recieved < header->body_size) { 
            rv = recv(sessfd, ((char *) body) + bytes_recieved, header->body_size - bytes_recieved, 0);
            if (rv <= 0) {
                err(1, 0);
                break;
            }
            bytes_recieved += rv;
        }

        switch (header->code)
        {
            case OPEN:
                fprintf(stderr, "Open Recieved\n");
                rpc_open(sessfd, header, body);
                break;

            case READ:
                fprintf(stderr, "Read Recieved\n");
                rpc_read(sessfd, header, body);
                break;
            
            case CLOSE:
                fprintf(stderr, "Close Recieved\n");
                rpc_close(sessfd, header, body);
                break;
            
            case WRITE:
                fprintf(stderr, "Write Recieved\n");
                rpc_write(sessfd, header, body);
                break;
            
            case UNLINK:
                rpc_unlink(sessfd, header, body);
                fprintf(stderr, "Unlink Recieved\n");
                break;
            
            case STAT:
                fprintf(stderr, "Stat Recieved\n");
                rpc_stat(sessfd, header, body);
                break;

            case LSEEK:
                fprintf(stderr, "Lseek Recieved\n");
                rpc_lseek(sessfd, header, body);
                break;
            
            case GETDIRENTRIES:
                fprintf(stderr, "Getdirentries Recieved\n");
                rpc_getdirentries(sessfd, header, body);
                break;
            
            default:
                break;
        }

        free(header);
        free(body);

    }
   
    

}

int main(int argc, char**argv) {
    
    char *serverport;
    unsigned short port;
    int sockfd, sessfd, rv, i;
    struct sockaddr_in srv, cli;
    socklen_t sa_size;
    
    // Get environment variable indicating the port of the server
    serverport = getenv("serverport15440");
    if (serverport) port = (unsigned short)atoi(serverport);
    else port=15213;
    
    // Create socket
    sockfd = socket(AF_INET, SOCK_STREAM, 0);   // TCP/IP socket
    if (sockfd<0) err(1, 0);            // in case of error
    
    // setup address structure to indicate server port
    memset(&srv, 0, sizeof(srv));           // clear it first
    srv.sin_family = AF_INET;           // IP family
    srv.sin_addr.s_addr = htonl(INADDR_ANY);    // don't care IP address
    srv.sin_port = htons(port);         // server port
    
    // bind to our port
    rv = bind(sockfd, (struct sockaddr*)&srv, sizeof(struct sockaddr));
    if (rv<0) err(1,0);
    
    // start listening for connections
    rv = listen(sockfd, 5);
    if (rv<0) err(1,0);
    
    // main server loop, handle clients one at a time, quit after 10 clients
    while (1) {
        
        // wait for next client, get session socket
        sa_size = sizeof(struct sockaddr_in);
        sessfd = accept(sockfd, (struct sockaddr *)&cli, &sa_size);
        if (sessfd<0) err(1,0);

        if (fork() == 0) {
            close(sockfd);
            handle_connection(sessfd);
            close(sessfd);
            exit(0);
        }
        close(sessfd);
        
    }

    close(sockfd);

    return 0;
}