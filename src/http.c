#include <stdio.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <netdb.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>

#include "http.h"

#define BUF_SIZE 1024

int client_socket(char *host, char *addrport_string) // Execute socket() and connect() and return the socket ID
{
    struct addrinfo their_addrinfo;         // Server address info
    struct addrinfo *their_addr = NULL;     // Connector's address information
    memset(&their_addrinfo, 0, sizeof(struct addrinfo));    //  Zero inf
    their_addrinfo.ai_family = AF_INET;         // Use an internet address
    their_addrinfo.ai_socktype = SOCK_STREAM;   // Use TCP rather than datagram
     if ((getaddrinfo(host, addrport_string, &their_addrinfo, &their_addr)) == -1)  //  Get IP address information
    {
        perror("AddressInf Error");  // IP address error check
        exit(1);
    }

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);  // Socket descriptor with IPV4, TCP
    if (sockfd == -1)
    {
        perror("Socket Error");     // Socket error check
        exit(1);
    }

    if (connect(sockfd, their_addr->ai_addr, their_addr->ai_addrlen)  == -1)    // Connect to their IP adress (Server)
    {
        perror("Connect Error");    // Connection error check
        exit(1);
    }

    //Clean up and close everything
    freeaddrinfo(their_addr);

    return sockfd;
}

/**
 * Perform an HTTP 1.0 query to a given host and page and port number.
 * host is a hostname and page is a path on the remote server. The query
 * will attempt to retrievev content in the given byte range.
 * User is responsible for freeing the memory.
 *
 * @param host - The host name e.g. www.canterbury.ac.nz
 * @param page - e.g. /index.html
 * @param range - Byte range e.g. 0-500. NOTE: A server may not respect this
 * @param port - e.g. 80
 * @return Buffer - Pointer to a buffer holding response data from query
 *                  NULL is returned on failure.
 */

Buffer* http_query(char *host, char *page, const char *range, int port) {

    Buffer* buffer = (Buffer *)malloc(sizeof(Buffer));  //  Allocate memory for the buffer
    buffer->data = (char *)calloc(BUF_SIZE, sizeof(char));
    buffer->length = BUF_SIZE;

    char addrport_string[12];
    sprintf(addrport_string, "%d", port);       //  Address Port

    int sockfd = client_socket(host, addrport_string);

    int received_buffer_strlen = BUF_SIZE;         //   Maximum Buffer size for Recieve data
    char* received_buffer = (char *)calloc(received_buffer_strlen, sizeof(char));  //   Allocated space for Recieved data
    sprintf(received_buffer, "GET /%s HTTP/1.0\r\nHost: %s\r\nRange: bytes=%s\r\nUser-Agent: getter\r\n\r\n", page, host, range); // HTTP Header
    write(sockfd, received_buffer, received_buffer_strlen);         //  Send the recieved Buffer to Server

    int recvd_file = 0;                     //  Record total received data
    while(1)    //  Looping recieve data
    {

        if (recvd_file > buffer->length - BUF_SIZE)    //  The size of recved_file larger then buffer length need to add extra 1024
        {
            buffer->length += BUF_SIZE;                 // Add 1024
            buffer->data = realloc(buffer->data, buffer->length); //  Realloc space for buffer context
        }

        int num_bytes = read(sockfd, buffer->data + recvd_file, BUF_SIZE);  // Record the number of bytes
        if (num_bytes == 0) break;      //  Break loop, if no more data recieved
        recvd_file += num_bytes;
    }

    buffer->length = recvd_file;  // Updata the final length

    //Clean up and Close everything
    close(sockfd);
    free(received_buffer);

    return buffer;
}


/**
 * Separate the content from the header of an http request.
 * NOTE: returned string is an offset into the response, so
 * should not be freed by the user. Do not copy the data.
 * @param response - Buffer containing the HTTP response to separate
 *                   content from
 * @return string response or NULL on failure (buffer is not HTTP response)
 */
char* http_get_content(Buffer *response) {

    char* header_end = strstr(response->data, "\r\n\r\n");

    if (header_end) {
        return header_end + 4;
    }
    else {
        return response->data;
    }
}


/**
 * Splits an HTTP url into host, page. On success, calls http_query
 * to execute the query against the url.
 * @param url - Webpage url e.g. learn.canterbury.ac.nz/profile
 * @param range - The desired byte range of data to retrieve from the page
 * @return Buffer pointer holding raw string data or NULL on failure
 */
Buffer *http_url(const char *url, const char *range) {
    char host[BUF_SIZE];
    strncpy(host, url, BUF_SIZE);

    char *page = strstr(host, "/");

    if (page) {
        page[0] = '\0';
        ++page;

        return http_query(host, page, range, 80);
    }
    else {

        fprintf(stderr, "could not split url into host/page %s\n", url);
        return NULL;
    }
}


/**
 * Makes a HEAD request to a given URL and gets the content length
 * Then determines max_chunk_size and number of split downloads needed
 * @param url   The URL of the resource to download
 * @param threads   The number of threads to be used for the download
 * @return int  The number of downloads needed satisfying max_chunk_size
 *              to download the resource
 */



int get_num_tasks(char *url, int threads) {
    char host[BUF_SIZE];
    strncpy(host, url, BUF_SIZE);   // Copy url pointer to host pointer
    char *page = strstr(host, "/"); // Get Page form url

    if (page) {
        page[0] = '\0';
        ++page;

        int port = 80;
        char addrport_string[12];
        sprintf(addrport_string, "%d", port);                   // Address Port
        int sockfd = client_socket(host, addrport_string);      // Call sockfd
        int received_buffer_strlen = BUF_SIZE;                  // Maximum Buffer size for Recieve data
        char* received_buffer = (char *)calloc(received_buffer_strlen, sizeof(char));  // Allocated space for Recieved data

        sprintf(received_buffer, "HEAD /%s HTTP/1.0\r\nHOST: %s\r\nUser-Agent: getter\r\n\r\n", page, host);  // HTTP Header
        write(sockfd, received_buffer, received_buffer_strlen);         // Send the recieved Buffer to Server

        char recv_buffer[BUF_SIZE] = "\0";         // Received buffer from Server
        int content_length = 0;                    // Recorded Content Length
        int content_len = strlen("Content-Length:");
        int num_bytes;                             // Record the number of bytes from Server
        while (1) {         // Break loop, if no more data recieved
            char *current_position = strstr(recv_buffer, "Content-Length:");  // Pointer for Content-Length's position
            if (current_position) {
                content_length = atoi(current_position + content_len);        // Pointer to Specific Number of Length
            }
            num_bytes = read(sockfd, recv_buffer, BUF_SIZE);       // Record the number of bytes from Server
            if (num_bytes == 0) break;         // Break loop, if no more data recieved
        }
        max_chunk_size = content_length  / threads + 1 ;   // Max Chunk Size add extra 1 byte for just in case of losing bytes

        //Clean up and Close everything
        close(sockfd);
        free(received_buffer);
        return threads;
    }
    return -1;
}


int get_max_chunk_size() {
    return max_chunk_size;
}
