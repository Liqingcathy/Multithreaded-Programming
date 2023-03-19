/**
 * Liqing Li
 * 3/14/2023
 * CPSC 5042 01 23WQ Comp Systems Principles II
 * Build a multithreading client and server program using Socket communication that
 * receives and handles multiple HTTP requests and read info through Logger pipe additionally.
 */
#include "http_context.h"
#include <iostream>
#include "string"
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <vector>
#include <sstream>
#include <fstream>
#include <map>
#include <sys/resource.h>
#include <sys/mman.h>
#include <stdio.h>

#define TRUE 1
#define PORT 8080
#define NUM_THREAD 6
pthread_t children[NUM_THREAD];

using namespace std;
pthread_mutex_t lock1;
struct sockaddr_in address{};
char buffer[50000];
vector<string> reqs;
int fd[2];

[[noreturn]] void *read(void *param);
void createSocketServer(int socket);
void sendNewSocketToThreads(http_context ctx);
void *handleRequest(void *params);
string handleFileRequest(const string &ext, http_context context);
void readFileAndSendResHeaderToClient(const string &ext, http_context ctx);
string readFileSendData(http_context ctx);
void PrintoutHeader(const string& res_header);
int sendHeaderStr(int sk, const string &msg);
map<string, string> getEnvVar();
string convertEnvToTable(map<string,string> env_map);
string handleEnv(http_context ctx);
string handleQueryQuote(http_context ctx);
string getQuotes(http_context ctx, int start, int length);
string handleShakespeare(http_context context);
void handleQuit(http_context context);
string formHttpHeader(http_context context);
void writeToFd(int i, string basicString);
void fileNotFound(http_context context, string logErr);
void sendBadRequestToClient(http_context context);
vector<string> split(string src, char d);
long get_tid_xplat();

static const char *txt_contents = nullptr;
extern char** environ;

string TXT_NAME = "shakespeare.txt";
string HTTP_VERSION_CODE = "HTTP/1.0 200 OK\n";
string LOG_ERR_404 = "HTTP/1.0 404 File Not Found\n";

int main() {
    vector<string> res;
    int socket;
    cout << "Main process " << getppid() <<  " | Main thread tid: " <<  get_tid_xplat() << endl;
    pipe(fd);

    // Pipe creates Logger thread that reads information from other thread file descriptors
    if (pipe(fd) < 0) {
        perror("Pipe build failed");
    } else if (pipe(fd) == 0) {
        pthread_t log;
        //create logger thread of the current main thread
        if (pthread_create(&log, nullptr, read, nullptr) != 0) {
            perror("Failed Logger thread build%s");
        }
    }
    // Create multi-threaded sockets with server socket
    createSocketServer(socket);

    // Clear memory and mutex and close socket.
    munmap((void *) txt_contents, strlen(txt_contents));
    pthread_mutex_destroy(&lock1);
    shutdown(socket, SHUT_RDWR);
    return 0;
}

/**
 * Connects and creates client sockets using socket(), bind(), listen(), and accept() functions.
 * @param sockfd socket descriptor
 */
void createSocketServer(int sockfd) {
    http_context context = {0};
    int addrlen;
    //1.create socket object
    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0){
        perror("socket failed");
        exit(1);
    }
    context.server_sock = sockfd;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    //2. Binds socket to the address and port number specified in addr
    if (::bind(sockfd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        perror("bind failed");
        exit(1);
    }

    //3. waits for the client to approach the server to make a connection
    string msg1 = "Listening on port: " + to_string(PORT) +"\n";
    write(fd[1], msg1.c_str(), msg1.length());

    if (listen(sockfd, NUM_THREAD) < 0) {
        perror("listen failed");
    }

    int c_socket;
    addrlen = sizeof(address); //Accepting the Incoming Connection
    while (TRUE) {
        //4. returns a new client socket descriptor referring to the socket
        // At this point, the connection is established between client and server, and they are ready to transfer data.
        if ((c_socket = accept(sockfd, (struct sockaddr *) &address, (socklen_t *) &addrlen)) < 0) {
            perror("Accept failed");
            exit(1);
        }

        if (pthread_mutex_init(&lock1, nullptr) != 0) {
            perror("lock init failed\n");
        }

        context.client_sock = c_socket;
        //send new socket to thread individually to handle request
        sendNewSocketToThreads(context);
    }
    close(context.client_sock);
}

/**
 * Create separate threads to handle each HTTP request
 * @param socketid socket server id int
 * @param request HTTP requests
 * @param readval buffer reads HTTP request
 */
void sendNewSocketToThreads(http_context ctx) {
    pthread_attr_t attrreq;
    pthread_attr_init((pthread_attr_t *) &attrreq);
    int i = 0;
    if (pthread_create(&children[i], nullptr, handleRequest, &ctx) != 0) {
        perror("Thread creation failed");
    }
    i ++;
}

/**
 * Receives HTTP request from new socket in each thread and
 * writes to file descriptors in order to read in the Logger thread.
 * Calls readFileAndSendResHeaderToClient helper function to handle requests.
 * @param params http_context struct
 */
void *handleRequest(void *params) {
    http_context ctx = *((http_context*)(params));
    pthread_mutex_lock(&lock1);
    read(ctx.client_sock, buffer, 1024);//read request from new created socket, originally set to 1024
    pthread_mutex_unlock(&lock1);

    reqs = split(buffer, '/');
    string uri = split(reqs[1], ' ')[0];
    string http_request = reqs[0] +  "/" + uri + " ";
    ctx.request_uri = uri;
    writeToFd(fd[1], http_request);

    //Handle HTTP requests based on file extensions and read files
    vector<string> file_extension = split(uri, '.');
    string ext = file_extension[file_extension.size()-1];
    readFileAndSendResHeaderToClient(ext, ctx);

    pthread_exit(0);
}


/**
 * Receives response message after reading files based on http_context attributes and file extension.
 * Calls Printout header helper function at the end for server logger.
 * @param ext file extension string
 * @param ctx http_context struct
 */
void readFileAndSendResHeaderToClient(const string &ext, http_context ctx) {

    if (ctx.request_uri == "env") {
        ctx.content_type = "text/html";
        ctx.headers["Success"] = handleEnv(ctx);
    } else if (ctx.request_uri == "shakespeare.txt") {
        ctx.content_type = "text/plain";
        ctx.headers["Success"]= handleShakespeare(ctx);
    }else if (ctx.request_uri.find('?') != string::npos) {
        ctx.content_type = "text/plain";
        ctx.headers["Success"] = handleQueryQuote(ctx);
    }else if (ext == "quit"){
        ctx.content_type = "text/plain";
        handleQuit(ctx);
    }else {
        ctx.headers["Success"] = handleFileRequest(ext, ctx);
    }
    PrintoutHeader(ctx.headers["Success"]);
}

/**
 * Helper function handles file extensions included in index.html, css, js, png, ico
 * @param ext string file extension
 * @param ctx http_context struct
 * @return success header message
 */
string handleFileRequest(const string &ext, http_context ctx) {
    if (ext == "html") {
        ctx.content_type = "text/html";
    } else if (ext == "css") {
        ctx.content_type = "text/css";
    } else if (ext == "js") {
        ctx.content_type = "text/javascript";
    } else if (ext == "png") {
        ctx.content_type = "image/png";
    } else if (ext == "ico") {
        ctx.content_type = "text/plain";
    }
    pthread_mutex_lock(&lock1);
    ctx.headers["Success"] = readFileSendData(ctx);
    pthread_mutex_unlock(&lock1);

    return ctx.headers["Success"];
}


/**
 * Reads file and send request header and body message separately to client socket.
 * Also handles 404 error for non-existing files and shut down the program.
 * @param pat resource path name
 * @param content_tp content type name
 * @param sk client socket id
 * @return response header string to log out info
 */
string readFileSendData(http_context ctx) {
    ifstream input_file(ctx.request_uri, ios::binary);
    if (input_file.is_open()) {
        input_file.seekg(0, ios::end);
        int file_size = input_file.tellg();
        input_file.seekg(0, ios::beg);
        input_file.seekg(0, ios::beg);
        ctx.content_length = file_size;
        string sendHeader = formHttpHeader(ctx);
        sendHeaderStr(ctx.client_sock, sendHeader);

        char *content = new char[file_size];
        input_file.read(content, file_size);
        string temp(content, file_size);
        sendHeaderStr(ctx.client_sock, temp);
        input_file.close();
        return sendHeader;

    } else {
        fileNotFound(ctx, "404 File Not Found.");
        return "";
    }
}

/**
 * Handles query parameter request by extracting a substring using given start and length.
 * Send bad request when parameter query or value is missing.
 * @param ctx http_context struct
 * @return header string for logger info
 */
string handleQueryQuote(http_context ctx){
    int start_pos = ctx.request_uri.find("start=");
    int length_pos = ctx.request_uri.find("&length=");
    int star_index = -1;
    int length_index = -1;
    string start_query_num = ctx.request_uri.substr(start_pos + 6, length_pos - start_pos - 6);
    string length_query_num = ctx.request_uri.substr(length_pos + 8);

    if (start_pos == string::npos || length_pos == string::npos || ctx.request_uri.find('&') == string::npos || ctx.request_uri.find('=') == string::npos) {
        sendBadRequestToClient(ctx);
    }else {
        try {
            star_index = stoi(start_query_num);
        } catch (const invalid_argument& e) {
            sendBadRequestToClient(ctx);
        }

        try {
            length_index = stoi(length_query_num);
        }catch (const invalid_argument& e) {
            sendBadRequestToClient(ctx);
        }
    }

    if (star_index == -1 || length_index == -1) {
        sendBadRequestToClient(ctx);
    }

    int start = stoi(ctx.request_uri.substr(start_pos + 6, length_pos - start_pos - 6));
    int length = stoi(ctx.request_uri.substr(length_pos + 8));

    string quote = getQuotes(ctx, start, length);
    ctx.content_length = quote.length();
    ctx.response_payload = quote;

    string sendHeader = formHttpHeader(ctx);
    sendHeaderStr(ctx.client_sock, sendHeader);
    sendHeaderStr(ctx.client_sock, ctx.response_payload);
    return sendHeader;
}

/**
 * A helper function that sends "Bad request" to web client.
 * @param ctx http_context struct
 */
void sendBadRequestToClient(http_context ctx) {
    ctx.headers["Bad"] = "HTTP/1.0 400 Bad Request\nContent-Length: 16\nContent-Type: text/plain\n\n";
    string logMsg = "400 Bad Request.";
    sendHeaderStr(ctx.client_sock, ctx.headers["Bad"] );
    sendHeaderStr(ctx.client_sock, logMsg);
    writeToFd(fd[1],logMsg);
    //return;
    throw runtime_error("Bad request");
}

/**
 * A helper function that opens txt file and read quote using buffer.
 * @param ctx http_context struct
 * @param start int index start position
 * @param length int length of the substring
 * @return string a the quote
 */
string getQuotes(http_context ctx, int start, int length) {
    ifstream file(TXT_NAME, ios::binary);
    if (!file) {
        throw runtime_error("Unable to open file");
    }

    file.seekg(0, ios::end);
    int file_size = file.tellg();
    int data_size = min(length, file_size - start);
    char* txt_buffer = new char[data_size];
    file.seekg(start);
    file.read(txt_buffer, data_size);
    string quote(txt_buffer, data_size);
    delete[] txt_buffer;

    return quote;
}

/**
 * Handles /env endpoint request first calling helper function to convert info
 * to table and send header to the web client.
 * @param ctx http_context
 * @return string header message
 */
string handleEnv(http_context ctx) {
    map<string, string> pair = getEnvVar();
    string table_str = convertEnvToTable(pair);
    string h1_msg = "<h1>Environment information</h1>\n";
    string h2_memory = "<h2>Memory</h2>\n";

    struct rusage usage;
    getrusage(RUSAGE_SELF, &usage);
    int max_rss_mb = usage.ru_maxrss / 1024;    // Max RSS in megabytes
    string memory_var = "Max size so far: ";
    memory_var += to_string(max_rss_mb);
    string response_body = "<html>\n<body>\n" + h1_msg + h2_memory + memory_var + "MB\n" + "\n<h2>Variables</h2>\n" + table_str + "</body>\n</html>\n";

    ctx.content_length = response_body.length();
    string sendHeader = formHttpHeader(ctx);
    sendHeaderStr(ctx.client_sock, sendHeader);
    sendHeaderStr(ctx.client_sock, response_body);
    return sendHeader;
}

/**
 * Converts environment variables into HTML table format
 * @param env_map stores string key and value
 * @return table string
 */
string convertEnvToTable(map<string, string> env_map) {
    string table;
    table += "<table>\n";
    for (auto& pair : env_map) {
        table += "<tr><td>" + pair.first + "</td><td>" + pair.second + "</td></tr>\n";
    }
    table += "</table>\n";
    return table;
}

/**
 * Gets key and value from system environment.
 * @return Map<string, string>
 */
map<string, string> getEnvVar() {
    map<string, string> env_vars;
    char** env = environ;
    while (*env != nullptr) {
        string env_var(*env++);
        size_t pos = env_var.find('=');
        if (pos != string::npos) {
            string key = env_var.substr(0, pos);
            string value = env_var.substr(pos + 1);
            env_vars[key] = value;
        }
    }
    return env_vars;
}

/**
 * Handles txt file request. Opens files and map the file into shared memory only when the first time access the endpoint.
 * Assigns http_context attributes value including content length, payload, header, etc.
 * Reads contents from already derived http_context values from the second time.
 * @param ctx http_context struct
 * @return header string for logger info
 */
string handleShakespeare(http_context ctx) {
//    static const char *txt_contents = nullptr;
    struct stat filestat;
    static bool mapped = false;

    if (!mapped) {
        FILE *file = fopen("shakespeare.txt", "r");
        if (!file) {
            perror("Failed to open file");
            return "";
        }

        if (-1 == fstat(fileno(file), &filestat)) {
            perror("Failed to get file size");
            fclose(file);
            return "";
        }

        if (filestat.st_size == 0) {
            perror("File is empty");
            fclose(file);
            return "";
        }

        ctx.content_length = filestat.st_size;
        txt_contents = (char *) mmap(0, ctx.content_length, PROT_READ, MAP_PRIVATE, fileno(file), 0);
        if (txt_contents == MAP_FAILED) {
            perror("Failed to mmap file");
            fclose(file);
        }
        mapped = true;
        fclose(file);
    }

    ctx.content_length = strlen(txt_contents);
    ctx.response_payload = txt_contents;
    string sendHeader = formHttpHeader(ctx);
    sendHeaderStr(ctx.client_sock, sendHeader);
    sendHeaderStr(ctx.client_sock, ctx.response_payload);

    return sendHeader;
}

/**
 * Handles /quit endpoint specifically to shut down the program
 * @param ctx http_context struct
 */
void handleQuit(http_context ctx) {
    string msg = "Shutting Down.";
    ctx.content_length = msg.length();
    string headerLine = formHttpHeader(ctx);
    sendHeaderStr(ctx.client_sock, headerLine);
    sendHeaderStr(ctx.client_sock, msg);
    write(fd[1], "200 OK", 6);
    sleep(1);
    exit(0);
}

/**
 * A critical section helper function that writes message to client socket
 * @param c_socket int client socket
 * @param message string information
 */
void writeToFd(int c_socket, string message) {
    pthread_mutex_lock(&lock1);
    write(c_socket, message.c_str(), message.length());
    pthread_mutex_unlock(&lock1);
}



/**
 * A helper function forms unified Success header string
 * @param ctx  http_context struct
 * @return string Success header value
 */
string formHttpHeader(http_context ctx) {
    ctx.headers["Success"] = HTTP_VERSION_CODE + "Content-Length: " + to_string(ctx.content_length) + "\n" + "Content-Type: " + ctx.content_type +  "\n\n";
    return ctx.headers["Success"];
}

/**
 *  Extracts string properly to log out required response message in server
 * @param res_header header string containing http version and status code
 */
void PrintoutHeader(const string& res_header) {
    if (!res_header.empty()) {
        vector<string> server_header = split(res_header, '\n\n');
        vector<string> header_stl = split(server_header[0], '\n');
        vector<string> status_code = split(header_stl[0], ' ');
        string server_res_msg = status_code[1] + " " + status_code[2] + " ";
        server_res_msg += server_header[2] + " " + server_header[1] + '\n';
        writeToFd(fd[1], server_res_msg);
    }
}


/**
 * A helper function that handles non-existing file or file extension.
 * @param ctx http_context
 * @param logErr string error message
 */
void fileNotFound(http_context ctx, string logErr) {
    ctx.headers["Error"] = LOG_ERR_404 + "Content-Length: 19\nContent-Type: text/plain\n\n";
    sendHeaderStr(ctx.client_sock, ctx.headers["Error"]);
    sendHeaderStr(ctx.client_sock, logErr);
    write(fd[1], logErr.c_str(), logErr.length());
}

/**
 * Send string data to client socket in char format
 * @param sk  client socket id
 * @param msg message to be sent to client socket
 * @return int 0 if successful, -1 otherwise
 */
int sendHeaderStr(int sk, const string &msg) {
    if (send(sk, msg.c_str(), msg.length(), 0) == -1) {
        cerr << "Send data failed " << endl;
        close(sk);
    }
}

/**
 * Reads buffer message that were written from socket server threads to print out
 * @param params
 */
[[noreturn]] void *read(void *params) {
    cout << "Logger pid: " << getpid() << " | Logger tid: " << get_tid_xplat() << endl;

    while (TRUE) {
        int index = 0;
        char output1[1024];
        ssize_t bytes_read = read(fd[0], output1, sizeof(output1));
        while (index < bytes_read) {
            cout << output1[index];
            index++;
        }
    }
}


/**
 * Split a string with given delimeter into vector string.
 * Function code credit to Professor Stephen from assignment instructions.
 * @param src input string
 * @param delim char delimeter
 * @return vector string
 */
vector<string> split(string src, char delim) {
    istringstream ss(src);
    vector<string> res;

    string piece;
    while(getline(ss, piece, delim)) {
        res.push_back(piece);
    }
    return res;
}

//Function code credit to Professor Stephen from assignment instructions.
long get_tid_xplat() {
#ifdef __APPLE__
    long   tid;
    pthread_t self = pthread_self();
    int res = pthread_threadid_np(self, reinterpret_cast<__uint64_t *>(&tid));
    return tid;
#else
    pid_t tid = gettid();
    return (long) tid;
#endif
}