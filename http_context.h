//
// Created by LIQING LI on 3/10/23.
//

#ifndef FINAL_HTTP_CONTEXT_H
#define FINAL_HTTP_CONTEXT_H

#include <string>
#include <map>
using namespace std;

struct http_context {
        int client_sock;
        int server_sock;
//        int fd;

        string request_uri;
        map<string, string> headers;
        //int status_code;
        string content_type;
        int content_length;
        string response_payload;
};


#endif //FINAL_HTTP_CONTEXT_H
