#include "request.h"
#include "response.h"
#include <string.h>


/******************************************************************************
 * ClientState-processing functions
 *****************************************************************************/
ClientState *init_clients(int n) {
    ClientState *clients = malloc(sizeof(ClientState) * n);
    for (int i = 0; i < n; i++) {
        clients[i].sock = -1;  // -1 here indicates available entry
    }
    return clients;
}

/* 
 * Remove the client from the client array, free any memory allocated for
 * fields of the ClientState struct, and close the socket.
 */
void remove_client(ClientState *cs) {
    if (cs->reqData != NULL) {
        free(cs->reqData->method);
        free(cs->reqData->path);
        for (int i = 0; i < MAX_QUERY_PARAMS && cs->reqData->params[i].name != NULL; i++) {

            /* ADDED TO STARTER CODE: The following two lines helps deal with the odd
             * case where the Fdata array of the client is non-null even after freeing;
             * the array elements progressively become more corrupted as more requests
             * are processed, ultimately resulting in a segmentation fault. */
            cs->reqData->params[i].name = NULL;
            cs->reqData->params[i].value = NULL;

            free(cs->reqData->params[i].name);
            free(cs->reqData->params[i].value);
        }
        free(cs->reqData);
        cs->reqData = NULL;
    }
    close(cs->sock);
    cs->sock = -1;
    cs->num_bytes = 0;
}


/*
 * Search the first inbuf characters of buf for a network newline ("\r\n").
 * Return the index *immediately after* the location of the '\n'
 * if the network newline is found, or -1 otherwise.
 * Definitely do not use strchr or any other string function in here. (Why not?)
 */
int find_network_newline(const char *buf, int inbuf) {

    // If we find \r, check if it is immediately followed by \n,
    // and return the index immediately following that of \n
    for (int index = 0; index < inbuf; index++) {
        if (buf[index] == '\r') {
            if (buf[index + 1] == '\n') {
                return index + 2;
            }
        } else {
            continue;
        }
    }
    // Failed to find network newline, so return -1.
    return -1;
}

/*
 * Removes one line (terminated by \r\n) from the client's buffer.
 * Update client->num_bytes accordingly.
 *
 * For example, if `client->buf` contains the string "hello\r\ngoodbye\r\nblah",
 * after calling remove_line on it, buf should contain "goodbye\r\nblah"
 * Remember that the client buffer is *not* null-terminated automatically.
 */
void remove_buffered_line(ClientState *client) {

    int new_start = 0;
    // If the client buffer is already empty, we should skip
    // looking for CRLF and just set everything in it to NULL.
    if (client->num_bytes == 0) {
        new_start = -1;
    } else {
        new_start = find_network_newline(client->buf, client->num_bytes);
    }

    // Deal with the buffer appropriately if a CRLF has or has not been detected in the buffer.
    if (new_start == -1) {
        memset(&client->buf, '\0', MAXLINE);
        client->num_bytes = 0;
    } else {
        client->num_bytes = client->num_bytes - new_start;
        memmove(client->buf, &client->buf[new_start], client->num_bytes);
        memset(&client->buf[client->num_bytes], '\0', MAXLINE - client->num_bytes);
    }
}


/*
 * Read some data into the client buffer. Append new data to data already
 * in the buffer.  Update client->num_bytes accordingly.
 * Return the number of bytes read in, or -1 if the read failed.

 * Be very careful with memory here: there might be existing data in the buffer
 * that you don't want to overwrite, and you also don't want to go past
 * the end of the buffer, and you should ensure the string is null-terminated.
 */
int read_from_client(ClientState *client) {
    remove_buffered_line(client);   // Before reading from the client, we should update the buffer to free up space.
    int read_bytes = read(client->sock, client->buf, MAXLINE - 1);
    if (read_bytes < 0) {
        perror("Unable to read from socket!");
        return -1;
    }
    client->num_bytes += read_bytes;
    return read_bytes;
}


/*****************************************************************************
 * Parsing the start line of an HTTP request.
 ****************************************************************************/
// Helper function declarations.
void parse_query(ReqData *req, const char *str);

void update_fdata(Fdata *f, const char *str);

void fdata_free(Fdata *f);

void log_request(const ReqData *req);


/* If there is a full line (terminated by a network newline (CRLF)) 
 * then use this line to initialize client->reqData
 * Return 0 if a full line has not been read, 1 otherwise.
 */
int parse_req_start_line(ClientState *client) {
    int crlf_exists = find_network_newline(client->buf, client->num_bytes);
    if (crlf_exists == -1) {
        return 0;
    }
        // If there is a network newline in the buffer, check if there is a valid
        // request method in it, and if so, store that in the client's reqData.
    else {
        char start[5] = {'\0'};
        if (strstr(client->buf, GET) == NULL) {
            if (strstr(client->buf, POST) == NULL) {
                perror("Could not find valid HTTP request method!");
                exit(1);
            } else {
                strncpy(start, POST, sizeof(POST));
            }
        } else {
            strncpy(start, GET, sizeof(GET));
        }
        // Initialize the memory for the reqData for the client.
        client->reqData = malloc(sizeof(ReqData));
        client->reqData->method = malloc(sizeof(char) * strlen(start));
        strcpy(client->reqData->method, start);
    }

    // Parse for any queries if they exist after the target.
    char *query_start = strstr(client->buf, "?");
    if (query_start != NULL) {
        parse_query(client->reqData, query_start + 1);
    }

    //Parse the HTTP target.
    char *target_start = strstr(client->buf, "/");
    if (target_start == NULL) {
        perror("Could not parse valid HTTP target!");
        exit(1);
    }
    char *target = strtok(target_start, " ?");
    client->reqData->path = malloc(sizeof(char) * strlen(target));
    strcpy(client->reqData->path, target);

    // This part is just for debugging purposes.
    ReqData *req = client->reqData;
    log_request(req);

    return 1;
}


/*
 * Initializes req->params from the key-value pairs contained in the given 
 * string.
 * Assumes that the string is the part after the '?' in the HTTP request target,
 * e.g., name1=value1&name2=value2.
 */
void parse_query(ReqData *req, const char *str) {
    // Since strtok operates on the given string, and since
    // the parameter str is a const, we need to copy it.
    char query[strlen(str)];
    strcpy(query, str);

    int index = 0;
    char *token = strtok(query, "=");

    // Until we reach 'HTTP' in the query string, keep parsing the queries.
    while (index < MAX_QUERY_PARAMS && strstr(token, "HTTP") == NULL) {
        Fdata *current = &req->params[index];
        current->name = malloc(sizeof(char) * strlen(token));
        strcpy(current->name, token);
        token = strtok(NULL, "& ");
        current->value = malloc(sizeof(char) * strlen(token));
        strcpy(current->value, token);
        token = strtok(NULL, "= ");
        index++;
    }

    // If there are fewer than MAX_QUERY_PARAMS queries, initialize the remaining queries to NULL.
    if (index != MAX_QUERY_PARAMS) {
        for (; index < MAX_QUERY_PARAMS; index++) {
            req->params[index].name = NULL;
            req->params[index].value = NULL;
        }
    }
}


/*
 * Print information stored in the given request data to stderr.
 */
void log_request(const ReqData *req) {
    fprintf(stderr, "Request parsed: [%s] [%s]\n", req->method, req->path);
    for (int i = 0; i < MAX_QUERY_PARAMS && req->params[i].name != NULL; i++) {
        fprintf(stderr, "  %s -> %s\n",
                req->params[i].name, req->params[i].value);
    }
}


/******************************************************************************
 * Parsing multipart form data (image-upload)
 *****************************************************************************/

char *get_boundary(ClientState *client) {
    int len_header = strlen(POST_BOUNDARY_HEADER);

    while (1) {
        int where = find_network_newline(client->buf, client->num_bytes);
        if (where > 0) {
            if (where < len_header || strncmp(POST_BOUNDARY_HEADER, client->buf, len_header) != 0) {
                remove_buffered_line(client);
            } else {
                // We've found the boundary string!
                // We are going to add "--" to the beginning to make it easier
                // to match the boundary line later
                char *boundary = malloc(where - len_header + 1);
                strncpy(boundary, "--", where - len_header + 1);
                strncat(boundary, client->buf + len_header, where - len_header - 1);
                boundary[where - len_header] = '\0';
                return boundary;
            }
        } else {
            // Need to read more bytes
            if (read_from_client(client) <= 0) {
                // Couldn't read; this is a bad request, so give up.
                return NULL;
            }
        }
    }
    return NULL;
}


char *get_bitmap_filename(ClientState *client, const char *boundary) {
    int len_boundary = strlen(boundary);

    // Read until finding the boundary string.
    while (1) {
        int where = find_network_newline(client->buf, client->num_bytes);
        if (where > 0) {
            if (where < len_boundary + 2 ||
                strncmp(boundary, client->buf, len_boundary) != 0) {
                remove_buffered_line(client);
            } else {
                // We've found the line with the boundary!
                remove_buffered_line(client);
                break;
            }
        } else {
            // Need to read more bytes
            if (read_from_client(client) <= 0) {
                // Couldn't read; this is a bad request, so give up.
                return NULL;
            }
        }
    }

    int where = find_network_newline(client->buf, client->num_bytes);

    client->buf[where - 1] = '\0';  // Used for strrchr to work on just the single line.
    char *raw_filename = strrchr(client->buf, '=') + 2;
    int len_filename = client->buf + where - 3 - raw_filename;
    char *filename = malloc(len_filename + 1);
    strncpy(filename, raw_filename, len_filename);
    filename[len_filename] = '\0';

    // Restore client->buf
    client->buf[where - 1] = '\n';
    remove_buffered_line(client);
    return filename;
}


/*
 * Read the file data from the socket and write it to the file descriptor
 * file_fd.
 * You know when you have reached the end of the file in one of two ways:
 *    - search for the boundary string in each chunk of data read 
 * (Remember the "\r\n" that comes before the boundary string, and the 
 * "--\r\n" that comes after.)
 *    - extract the file size from the bitmap data, and use that to determine
 * how many bytes to read from the socket and write to the file
 */
int save_file_upload(ClientState *client, const char *boundary, int file_fd) {
    // Read in the next two lines: Content-Type line, and empty line
    remove_buffered_line(client);
    remove_buffered_line(client);

    // Set up a string for use to test if the end of the request has been reached ("\r\n<boundary>--\r\n")
    char *boundary_end_string = malloc(sizeof(char) * (strlen(boundary) + 6));
    strcpy(boundary_end_string, "\r\n");
    strcat(boundary_end_string, boundary);
    strcat(boundary_end_string, "--\r\n");

    // Write out what's currently in the buffer (the first part of the bitmap data) before we start reading.
    write(file_fd, client->buf, sizeof(char) * client->num_bytes);

    int bytes_read = 0;
    int boundary_flag = 0;
    // Keep reading and writing from the socket until we find the boundary termination string.
    while (boundary_flag == 0) {
        bytes_read = read(client->sock, client->buf, MAXLINE);
        char *test_results = strstr(client->buf, boundary_end_string);
        if (test_results != NULL) {
            boundary_flag = 1;
            *test_results = '\0';
            write(file_fd, client->buf, sizeof(char) * strlen(client->buf));
        } else {
            write(file_fd, client->buf, sizeof(char) * bytes_read);
        }
    }
    free(boundary_end_string);
    return 0;
}


