#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main() {
    char* content_length = getenv("CONTENT_LENGTH");
    char* query = getenv("QUERY_STRING");

    int len = 0;
    if(content_length != NULL){
        len = atoi(content_length);
    }

    char body[len + 1];

    int nread = read(0, body, len);

    body[len] = '\0';

    char response[500];

    sprintf(response, "Hello CS324\nQuery string: %s\nRequest body: %s", query, body);

    fprintf(stdout, "Content-Type: text/plain\r\n");

    fprintf(stdout, "Content-Length: %ld\r\n\r\n", strlen(response));

    fprintf(stdout, "%s", response);
}