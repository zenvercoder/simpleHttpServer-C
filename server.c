//
// server.c
//
// Computer Science 50
// Problem Set 6
//

// feature test macro requirements
//these allow us to use certain functions that are declared (conditionally) in the header files further below.
//#define _GNU_SOURCE
//#define _XOPEN_SOURCE 700
//#define _XOPEN_SOURCE_EXTENDED

// constants that specify limits on HTTP requests sizes
// limits on an HTTP request's size, based on defaults used by Apache's (a popular web server) values
// http://httpd.apache.org/docs/2.2/mod/core.html
#define LimitRequestFields 50
#define LimitRequestFieldSize 4094
#define LimitRequestLine 8190

//constant the specifies how many bytes we’ll eventually be reading into buffers at a time.
// number of bytes for buffers, below BYTES is an 8-bit char
#define BYTES 512

// header files
#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h> // a global variable used by quite a few functions to indicate (via an int), in cases of error, precisely which error has occurred
#include <limits.h>
#include <math.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <ctype.h>

// types
typedef char BYTE;

// prototypes
bool connected(void);
void error(unsigned short code);
void freedir(struct dirent** namelist, int n);
void handler(int signal);
char* htmlspecialchars(const char* s);
char* indexes(const char* path);
void interpret(const char* path, const char* query);
void list(const char* path);
bool load(FILE* file, BYTE** content, size_t* length);
const char* lookup(const char* path);
bool parse(const char* line, char* path, char* query);
const char* reason(unsigned short code);
void redirect(const char* uri);
bool request(char** message, size_t* length);
void respond(int code, const char* headers, const char* body, size_t length);
void start(short port, const char* path);
void stop(void);
void transfer(const char* path, const char* type);
char* urldecode(const char* s);

// server's root.. a pointer to the string that represents the root of the server. 
// ex: public root would be a pointer to that public directory
char* root = NULL;

// file descriptor for sockets. similar to file* fp... reads from network connections 
// that use integers instead of pointers. They are global to keep track of ct file descriptor
int cfd = -1, sfd = -1;

// flag indicating whether control-c has been heard. 
bool signaled = false;

int main(int argc, char* argv[])
{
    // a global variable defined in errno.h that's "set by system 
    // calls and some library functions [to a nonzero value]
    // in the event of an error to indicate what went wrong"
    errno = 0;

    // default to port 8080
    int port = 8080;

    // usage
    const char* usage = "Usage: server [-p port] /path/to/root";

    // parse command-line arguments
    int opt;
    // getopt a function declared in unistd.h that makes it easier to parse command-line arguments.
    while ((opt = getopt(argc, argv, "hp:")) != -1)
    {
        switch (opt)
        {
            // -h
            case 'h':
                printf("%s\n", usage);
                return 0;

            // -p port
            case 'p':

                port = atoi(optarg);

                break;
        }
    }

    // ensure port is non-negative and path to server's root is specified
    if (port < 0 || port > SHRT_MAX || argv[optind] == NULL || strlen(argv[optind]) == 0)
    {
        // announce usage
        printf("%s\n", usage);
        // return 2 just like bash's builtins
        return 2;
    }

    // start server// magic happens
    start(port, argv[optind]);

    // listen for SIGINT (aka control-c) //listen for a signal if control c, function called handler that stops program
    struct sigaction act;
    act.sa_handler = handler;
    act.sa_flags = 0;
    sigemptyset(&act.sa_mask);
    sigaction(SIGINT, &act, NULL);

    // a message and its length
    char* message = NULL;
    size_t length = 0;

    // path requested
    char* path = NULL;

    // accept connections one at a time 
    // loops infinitely, waiting for someone to connect/waiting for true to be returned by this function
    while (true)
    {
        // free last path, if any that might have been allocated by previous iteration of loop
        if (path != NULL)
        {
            printf("146 path= [%s]\n", path);
            free(path);
            path = NULL;
        }

        // free last message, if any
        if (message != NULL)
        {
            // printf("153 message= [%s]\n", message);
            free(message);
            message = NULL;
        }
        length = 0;

        // close last client's socket, if any
        if (cfd != -1)
        {
            printf("162 cfd= [%i]\n", cfd);
            close(cfd);
            cfd = -1;
        }

        // check for control-c
        if (signaled)
        {
            printf("171 stop signaled");
            stop();
        }

        // check whether client has connected 
        // function they wrote, loops infinitely, waiting for true to be returned by that function
        // if a ct or browser or even curl has connected to the server
        if (connected())
        {
            // check for request // takes whatever is inside virtual envelope (http request), 
            // parses initial request top to bottom left to right, loads all of intial lines 
            // into a variable called message and load its length into length. 
            // the request(function) takes two arguments, message and length. they both have &infront. 
            // scroll up, message and length are declared inside of main to be a char* and a size_t (an int).
            // message is a char* but I'm passing in &, i'm getting the address of a pointer... a double pointer. 
            // scroll down.. the reqeust function returns a bool. char** because i want this function request to 
            // allocate memory for however big the http request from the virtual envelope that it receives from the browser 
            // I want to be able to return a string but i also want to be able to return a length. 
            // you can return two values if you pass in two values by reference or by pointer 
            // so you can go to those addresses, put values there. message = char**, as soon as you go there with *message, 
            // you're going to find a chunk of memory that should be a char* or the address of a string
            if(request(&message, &length))
            {
                printf("188 request function called\n");
                // extract message's request-line
                // http://www.w3.org/Protocols/rfc2616/rfc2616-sec5.html // the following will be useful in your own parse function. strstr allows you to search for one string in another, this is their way of searching for the end of a line so that I can read just one line into memory. 
                const char* haystack = message;
                // printf("198 haystack = [%s]\n", haystack);
                const char* needle = strstr(haystack, "\r\n");
                // printf("200 needle = [%s]\n", needle);
                if (needle == NULL)
                {
                    printf("203 (approx line) error 500 perhaps something wrong with needle/haystack\n");
                    error(500);
                    continue;
                }
                char line[needle - haystack + 2 + 1]; // allocate memory for the request (haystack - needle)
                // (make memory for the line)
                strncpy(line, haystack, needle - haystack + 2);
                // line[needle - haystack + 2] = '\0'; // store in this array, that first line

                // log request-line
                printf("211 request-line: [%s]\n", line);

                // parse request-line // the purpose is to take the very first line and extract the absolute path and query. 
                // request target is a string that can be broken up into two parts absolute-path like hello.html followed by 
                // an optional question mark
                char abs_path[LimitRequestLine + 1];
                // printf("215 abs_path(before parse) = [%s]\n", abs_path);
                // going to add null terminator in parse instead
                // abs_path[0] = '\0';
                // printf("219 abs_path(before parse, after appending null terminator) = [%s]\n", abs_path);
                char query[LimitRequestLine + 1];
                if (parse(line, abs_path, query))
                {
                    printf("223 result from parse... abs_path= [%s], query string = [%s]\n", abs_path, query);
                    // URL-decode absolute-path
                    char* p = urldecode(abs_path); // in case the browser has encoded characters in a special way, it turns it back to ascii characters
                    if (p == NULL)
                    {
                        printf("error from parse, approx line 224\n");
                        error(500);
                        continue;
                    }

                    // resolve absolute-path to local path 
                    // if user has requested /hello.html, what file do they really mean? take root of server, 
                    // that path to the public directory and concatenate it with something like hello.html so we have 
                    // one bigger string that leads us exactly to the hello.html file on cs50 ide harddrive or disk
                    // path = malloc(strlen(root) + strlen(p) + 1);
                    // length of ../server.c (no idea if this will work)
                    // path = malloc(16 + strlen(p) + 1);
                    path = malloc(strlen(root) + strlen(p) + 1);
                    // printf("240 path = [%s]\n", path); it was empty []
                    if (path == NULL)
                    {
                        printf("error 500 parse failed, approx line 243\n");
                        error(500);
                        continue;
                    }
                    //printf("247 root = [%s]\n", root);
                    strcpy(path, root);
                    // printf("249 path = [%s]\n", path);
                    printf("250 p = [%s]\n", p);

                    // hard-coding the path for now
                    // this affects how much memory to malloc
                    strcat(path, p);

                    free(p);

                    // printf("249 ***root from request/parse = [%s]\n", root);
                    printf("250 ***path from request/parse = [%s]\n", path);
                    printf("251 ***abs_path from request/parse = [%s]\n", abs_path);

                    // ensure path exists
                    if (access(path, F_OK) == -1)
                    {
                        printf("256 path = [%s]\n", path);
                        printf("257 error 404 parse failed, path does not exist\n");
                        error(404);
                        continue;
                    }

                    // if path to directory 
                    // has user requested a file or a directory? force user to be redirected to not 'foo' but 'foo/'
                    struct stat sb;
                    //printf("line 253 if(stat(path... about to be invoked. Below is indexes\n");
                    if (stat(path, &sb) == 0 && S_ISDIR(sb.st_mode))
                    {
                        // redirect from absolute-path to absolute-path/
                        if (abs_path[strlen(abs_path) - 1] != '/')
                        {
                            char uri[strlen(abs_path) + 1 + 1];
                            strcpy(uri, abs_path);
                            strcat(uri, "/");
                            redirect(uri);
                            continue;
                        }

                        // use path/index.php or path/index.html, if present, instead of directory's path 
                        // if user has visited a directory and that directory contains a file called index.html or .php, 
                        // we don't want to show them the contents of that directory, we want to show them the contents of 
                        // that default file index.html or .php. this function called index checks "is there a file in here 
                        // called index.html or .php?"
                        //printf("indexes (approx 271) about to be called\n");
                        char* index = indexes(path); 
                        if (index != NULL)
                        {
                            //printf("index value (line 276) = [%s]\n", index);
                            //printf("index value should be /home/ubuntu/workspace/pset6/public/index.html, not blank!\n");
                            free(path);
                            path = index;
                            //printf("path value (line 276) = [%s]\n", path);
                        }
                        // list contents of directory
                        else
                        {
                            printf("else loop executed (line 286ish)\n");
                            list(path);
                            continue;
                        }
                        //printf("line 290 if(stat(path... invoked. Did indexes get called?\n");
                    }

                    // look up MIME type for file at path 
                    // if user requests is not for a directory but for a file, lookup function tell the 
                    // server is this a jpeg? is this a gif? 
                    //printf("lookup, approx 298, called\n");
                    const char* type = lookup(path);
                    if (type == NULL)
                    {
                        printf("error from 302: const char* type = lookup(path), type == NULL\n");
                        error(501);
                        continue;
                    }
                    else
                    {
                        printf("from call to lookup in 304\n");
                        printf("type = %s, if != 501, call to lookup successfull, type != NULL\n", type);
                    }
                    // interpret PHP script at path 
                    // if the above is true, this will say is it a php file? then call function called interpret (staff wrote
                    // it interprets php file and spits out results
                    if (strcasecmp("text/x-php", type) == 0)
                    {
                        printf("query called approx 312\n");
                        interpret(path, query);
                    }
                    // if it's anything else, transfer the file from the server to the user as if they requested an html page, img, etc
                    // transfer file at path
                    else
                    {
                        transfer(path, type);
                    }
                }
            }
        }
    }
}

/**
 * Checks (without blocking) whether a client has connected to server. 
 * Returns true iff so.
 */
bool connected(void)
{
    struct sockaddr_in cli_addr;
    memset(&cli_addr, 0, sizeof(cli_addr));
    socklen_t cli_len = sizeof(cli_addr);
    cfd = accept(sfd, (struct sockaddr*) &cli_addr, &cli_len);
    if (cfd == -1)
    {
        return false;
    }
    return true;
}

/**
 * Responds to client with specified status code.
 */
void error(unsigned short code)
{
    // determine code's reason-phrase
    const char* phrase = reason(code);
    if (phrase == NULL)
    {
        return;
    }

    // template for response's content
    char* template = "<html><head><title>%i %s</title></head><body><h1>%i %s</h1></body></html>";

    // render template
    char body[(strlen(template) - 2 - ((int) log10(code) + 1) - 2 + strlen(phrase)) * 2 + 1];
    int length = sprintf(body, template, code, phrase, code, phrase);
    if (length < 0)
    {
        body[0] = '\0';
        length = 0;
    }

    // respond with error
    char* headers = "Content-Type: text/html\r\n";
    respond(code, headers, body, length);
}

/**
 * Frees memory allocated by scandir.
 * facilitate freeing memory that’s allocated by a function called scandir that we call in list.
 */
void freedir(struct dirent** namelist, int n)
{
    if (namelist != NULL)
    {
        for (int i = 0; i < n; i++)
        {
            free(namelist[i]);
        }
        free(namelist);
    }
}
 
/**
 * Handles signals.
 */
void handler(int signal)
{
    // if user hits control-c
    if (signal == SIGINT)
    {
        signaled = true;
    }
}

/**
 * Escapes string for HTML. Returns dynamically allocated memory for escaped
 * string that must be deallocated by caller.
 */
char* htmlspecialchars(const char* s)
{
    // ensure s is not NULL
    if (s == NULL)
    {
        return NULL;
    }

    // allocate enough space for an unescaped copy of s
    char* t = malloc(strlen(s) + 1);
    if (t == NULL)
    {
        return NULL;
    }
    t[0] = '\0';

    // iterate over characters in s, escaping as needed
    for (int i = 0, old = strlen(s), new = old; i < old; i++)
    {
        // escape &
        if (s[i] == '&')
        {
            const char* entity = "&amp;";
            new += strlen(entity);
            t = realloc(t, new);
            if (t == NULL)
            {
                return NULL;
            }
            strcat(t, entity);
        }

        // escape "
        else if (s[i] == '"')
        {
            const char* entity = "&quot;";
            new += strlen(entity);
            t = realloc(t, new);
            if (t == NULL)
            {
                return NULL;
            }
            strcat(t, entity);
        }

        // escape '
        else if (s[i] == '\'')
        {
            const char* entity = "&#039;";
            new += strlen(entity);
            t = realloc(t, new);
            if (t == NULL)
            {
                return NULL;
            }
            strcat(t, entity);
        }

        // escape <
        else if (s[i] == '<')
        {
            const char* entity = "&lt;";
            new += strlen(entity);
            t = realloc(t, new);
            if (t == NULL)
            {
                return NULL;
            }
            strcat(t, entity);
        }

        // escape >
        else if (s[i] == '>')
        {
            const char* entity = "&gt;";
            new += strlen(entity);
            t = realloc(t, new);
            if (t == NULL)
            {
                return NULL;
            }
            strcat(t, entity);
        }

        // don't escape
        else
        {
            strncat(t, s + i, 1);
        }
    }
    // escaped string
    return t;
}

/**
 * Checks, in order, whether index.php or index.html exists inside of path.
 * Returns path to first match if so, else NULL.
 */
char* indexes(const char* path)
{
    int length = strlen(path);
    const char* phpPath = "index.php";
    char* phpString = malloc(length + 9);
    strcat(phpString, path);
    strcat(phpString, phpPath);
        
    if(access(phpString, F_OK) == -1)
    {
        // TODO: free phpString here 
        // phpString = NULL;
        free(phpString);
        //printf("indexes function(approx 511) called... path (printed from indexes ~ ) = %s\n", path);   
        const char* htmlPath = "index.html";
        char* htmlString = malloc(length + 10);
        strcat(htmlString, path);
        strcat(htmlString, htmlPath);
        
        if(access(htmlString, F_OK) == -1)
        {
            printf("error 403 indexes failed (access(phpString + htmlString, F_OK)), approx line 523\n");
            printf("if != index.php or index.html, error 403 is what we want... approx line 523\n");
            error(403);
            htmlString = NULL;
            return NULL;  
        }
        else
        {
            printf("returning htmlString = [%s] (approx 558), \n", htmlString);   
            return htmlString;
        }
    }
    else
    {
        printf("returning phpString = [%s] (approx 558), \n", phpString);   
        return phpString;
    }
}

/**
 * Interprets PHP file at path using query string.
 */
void interpret(const char* path, const char* query)
{
    // ensure path is readable
    if (access(path, R_OK) == -1)
    {
        error(403);
        return;
    }

    // open pipe to PHP interpreter
    char* format = "QUERY_STRING=\"%s\" REDIRECT_STATUS=200 SCRIPT_FILENAME=\"%s\" php-cgi";
    char command[strlen(format) + (strlen(path) - 2) + (strlen(query) - 2) + 1];
    if (sprintf(command, format, query, path) < 0)
    {
        printf("error 500 interpret failed, approx line 586\n");
        error(500);
        return;
    }
    FILE* file = popen(command, "r");
    if (file == NULL)
    {
        printf("error 500 interpret failed, approx line 593\n");
        error(500);
        return;
    }

    // load interpreter's content
    char* content;
    size_t length;
    printf("load about to be invoked from interpret function, 601\n");
    if (load(file, &content, &length) == false)
    {
        printf("error 500 (perhaps load in interpret failed), approx line 604\n");
        error(500);
        return;
    }
    else
    {
         printf("call to load (in interpret function) was successful\n");
    }

    // close pipe
    pclose(file);

    // subtract php-cgi's headers from content's length to get body's length
    char* haystack = content;
    char* needle = strstr(haystack, "\r\n\r\n");
    if (needle == NULL)
    {
        free(content);
        printf("error 500 interpret failed, approx line 622\n");
        error(500);
        return;
    }

    // extract headers
    char headers[needle + 2 - haystack + 1];
    strncpy(headers, content, needle + 2 - haystack);
    headers[needle + 2 - haystack] = '\0';

    // respond with interpreter's content
    respond(200, headers, needle + 4, length - (needle - haystack + 4));

    // free interpreter's content
    free(content);
}

/**
 * Responds to client with directory listing of path.
 */
void list(const char* path)
{
    // ensure path is readable and executable
    if (access(path, R_OK | X_OK) == -1)
    {
        error(403);
        return;
    }

    // open directory
    DIR* dir = opendir(path);
    if (dir == NULL)
    {
        return;
    }

    // buffer for list items
    char* list = malloc(1);
    list[0] = '\0';

    // iterate over directory entries
    struct dirent** namelist = NULL;
    int n = scandir(path, &namelist, NULL, alphasort);
    for (int i = 0; i < n; i++)
    {
        // omit . from list
        if (strcmp(namelist[i]->d_name, ".") == 0)
        {
            continue;
        }

        // escape entry's name
        char* name = htmlspecialchars(namelist[i]->d_name);
        if (name == NULL)
        {
            free(list);
            freedir(namelist, n);
            printf("error 500 list() failed, approx line 624\n");
            error(500);
            return;
        }

        // append list item to buffer
        char* template = "<li><a href=\"%s\">%s</a></li>";
        list = realloc(list, strlen(list) + strlen(template) - 2 + strlen(name) - 2 + strlen(name) + 1);
        if (list == NULL)
        {
            free(name);
            freedir(namelist, n);
            printf("error 500 list failed, approx line 636\n");
            error(500);
            return;
        }
        if (sprintf(list + strlen(list), template, name, name) < 0)
        {
            free(name);
            freedir(namelist, n);
            free(list);
            printf("error 500 list failed, approx line 645\n");
            error(500);
            return;
        }

        // free escaped name
        free(name);
    }

    // free memory allocated by scandir
    freedir(namelist, n);

    // prepare response
    const char* relative = path + strlen(root);
    char* template = "<html><head><title>%s</title></head><body><h1>%s</h1><ul>%s</ul></body></html>";
    char body[strlen(template) - 2 + strlen(relative) - 2 + strlen(relative) - 2 + strlen(list) + 1];
    int length = sprintf(body, template, relative, relative, list);
    if (length < 0)
    {
        free(list);
        closedir(dir);
        printf("error 500 list failed, approx line 666\n");
        error(500);
        return;
    }

    // free buffer
    free(list);

    // close directory
    closedir(dir);

    // respond with list
    char* headers = "Content-Type: text/html\r\n";
    printf("from list\n");
    respond(200, headers, body, length);
}

/**
 * Loads a file into memory dynamically allocated on heap.
 * Stores address thereof in *content and length thereof in *length.
 */
bool load(FILE* file, BYTE** content, size_t* length)
{
    // TODO
    if(file == NULL)
    {
        return false;
    }

    // initialize content and its length
    *content = "";
    *length = 0;
    
    // BYTE buffer[BYTES];
    char* buffer = malloc(BYTES * sizeof(char));
    ssize_t bytesRead;
    
    do
    {
        bytesRead = fread(buffer, sizeof(BYTE), BYTES, file);
        printf("bytesRead = %zu\n", bytesRead);
         // append bytes to content 
        // *content = realloc(*content, bytesRead);
        // *content = realloc(*content, *length + bytesRead + 1);
        *content = malloc(sizeof(BYTE) * bytesRead);
        if (*content == NULL)
        {
            *length = 0;
            break;
        }
        // memcpy(*content, *buffer, bytesRead);
        // memcpy(*content + *length, buffer, bytesRead);
        *content = buffer;
        *length += bytesRead;
    }
    while(bytesRead == BYTES);
    
    
    // null-terminate content thus far
    *(*content + *length) = '\0';
    *content = buffer;
    
    printf("content(from load function) = [%s]\n", *content);
    printf("length(from load function) = [%zu]\n", *length);

    return true;
}

/**
 * Looks at path + file extension. Returns MIME type for supported extensions, else NULL.
 */
const char* lookup(const char* path)
{
    printf("779 path fr lookup()= %s\n", path);
    for (int i = 0; i < 50; i++)
    {
        //find the dot
        //changed from i + 1 to plain 'i'
        if (path[i] == 46)
        {
            int firstLetter = tolower(path[i + 1]);
            int secondLetter = tolower(path[i + 2]);
            if(firstLetter == 99) 
            {
                printf("790 firstLetter int fr lookup()= %i\n", firstLetter);
                return "text/css";
            }
            else if(firstLetter == 104)
            {
                printf("795 lookup found text/html\n");
                return "text/html";
            }
            else if(firstLetter == 106 && secondLetter == 115)
            {
                printf("800 lookup found text/javascript\n");
                return "text/javascript";
            }
            else if(firstLetter == 112 && secondLetter == 104)
            {
                return "text/x-php";
            }
            else if(firstLetter == 103)
            {
                return "image/gif";
            }
            else if(firstLetter == 105)
            {
                return "image/x-icon";
            }
            else if(firstLetter == 106 && secondLetter == 112)
            {
                return "image/jpeg";
            }
            else if(firstLetter == 112 && secondLetter == 110)
            {
                return "image/png";
            }
            else 
            {
                return NULL;
            }
        }
    }
    return 0;
}

/**
 * Parses a request-line, storing its absolute-path at abs_path 
 * and its query string at query, both of which are assumed
 * to be at least of length LimitRequestLine + 1.
 */
bool parse(const char* line, char* abs_path, char* query)
{
    printf("840 parse() called\n");
    if(line[0] != 'G' || line[1] != 'E' || line[2] != 'T' || line[3] != 32)
    {
        printf("405 Method Not Allowed\n");
        error(405);
        return false;
    }
    
    int charPosition = 4;
    bool queryFound = false;
    int q = 0;
    int pathIndex = 0;
    query[0] = '\0';
     
    do
    {
        // printf("860 check if target request contains a quotation mark\n");
        // if target request contains a quotation mark
        if(strchr(&line[charPosition], 34))
        {
            printf("400 Bad Request");
            error(400);
            return 1;
        }
        // printf("868 check if query found\n");
        if(queryFound)
        {
            query[q++] = line[charPosition];
            printf("867 query found %s\n", query);
        }
        //working on abs_path and looking for query
        else 
        {
            // if question mark, start loading into query array
            if(line[charPosition] == 63)
            {
                queryFound = true;
            }
            //start loading into abs_path array
            else if(queryFound != true)
            {
                // printf("886 loaded [%c] into abs_path array\n", line[charPosition]);
                abs_path[pathIndex++] = line[charPosition];
            }
        }
    }
    while(line[++charPosition] != 32);
    // add null terminator to abs_path array
    abs_path[pathIndex] = '\0';
    printf("892 abs_path= [%s]\n", abs_path);
    // add null terminator to query array
    query[q] = '\0';
    
    // to skip over space
    charPosition++;
    // if target does not start with '/'
    if(abs_path[0] != '/')
    {
        printf("501 Not Implemented(parse error)");
        error(501);
        return 1;
    }
    
    char version[9];
    // if pathComplete, then we are now handling version 
    for(int vi = 0; vi < 8; vi++)
    {
        version[vi] = line[charPosition + vi];
    }
    version[8] = '\0';
    // check if version is indeed HTTP 1.1
    char expectedVersion[] = "HTTP/1.1";
    // printf("914 version = [%s], expectedVersion = [%s], line = %s", version, expectedVersion, line);
    
    for(int i = 0; i < 8; i++)
    {
        // printf("912 version[i] = [%c]\n", version[i]);
        if(version[i] != expectedVersion[i])
        {

            printf("505 HTTP Version Not Supported(but parse did run)\n");
            error(505);
            return 1;
        }
    }
    //error(501);
    //return false
    printf("923 from parse: version = [%s], expectedVersion = [%s]\n", version, expectedVersion);
    printf("924 parse ran: abs_path = [%s]\n", abs_path);
    return true;
}

/**
 * Returns status code's reason phrase.
 *
 * http://www.w3.org/Protocols/rfc2616/rfc2616-sec6.html#sec6
 * https://tools.ietf.org/html/rfc2324
 */
const char* reason(unsigned short code)
{
    switch (code)
    {
        case 200: return "OK";
        case 301: return "Moved Permanently";
        case 400: return "Bad Request";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 414: return "Request-URI Too Long";
        case 418: return "I'm a teapot";
        case 500: return "Internal Server Error";
        case 501: return "Not Implemented";
        case 505: return "HTTP Version Not Supported";
        default: return NULL;
    }
}

/**
 * Redirects client to uri.
 */
void redirect(const char* uri)
{
    char* template = "Location: %s\r\n";
    char headers[strlen(template) - 2 + strlen(uri) + 1];
    if (sprintf(headers, template, uri) < 0)
    {
        printf("error 500 redirect failed, approx line 883\n");
        error(500);
        return;
    }
    respond(301, headers, NULL, 0);
}

/**
 * Reads (without blocking) an HTTP request's headers into memory dynamically allocated on heap.
 * Stores address thereof in *message and length thereof in *length.
 */
bool request(char** message, size_t* length)
{
    // ensure socket is open
    if (cfd == -1)
    {
        return false;
    }

    // initialize message and its length
    *message = NULL;
    *length = 0;

    // read message 
    while (*length < LimitRequestLine + LimitRequestFields * LimitRequestFieldSize + 4)
    {
        // read from socket
        BYTE buffer[BYTES];
        ssize_t bytes = read(cfd, buffer, BYTES);
        if (bytes < 0)
        {
            if (*message != NULL)
            {
                free(*message);
                *message = NULL;
            }
            *length = 0;
            break;
        }

        // append bytes to message 
        *message = realloc(*message, *length + bytes + 1);
        if (*message == NULL)
        {
            *length = 0;
            break;
        }
        memcpy(*message + *length, buffer, bytes);
        *length += bytes;

        // null-terminate message thus far
        *(*message + *length) = '\0';

        // search for CRLF CRLF
        int offset = (*length - bytes < 3) ? *length - bytes : 3;
        char* haystack = *message + *length - bytes - offset;
        char* needle = strstr(haystack, "\r\n\r\n");
        if (needle != NULL)
        {
            // trim to one CRLF and null-terminate
            *length = needle - *message + 2;
            *message = realloc(*message, *length + 1);
            if (*message == NULL)
            {
                break;
            }
            *(*message + *length) = '\0';

            // ensure request-line is no longer than LimitRequestLine
            haystack = *message;
            needle = strstr(haystack, "\r\n");
            if (needle == NULL || (needle - haystack + 2) > LimitRequestLine)
            {
                break;
            }

            // count fields in message
            int fields = 0;
            haystack = needle + 2;
            while (*haystack != '\0')
            {
                // look for CRLF
                needle = strstr(haystack, "\r\n");
                if (needle == NULL)
                {
                    break;
                }

                // ensure field is no longer than LimitRequestFieldSize
                if (needle - haystack + 2 > LimitRequestFieldSize)
                {
                    break;
                }

                // look beyond CRLF
                haystack = needle + 2;
            }

            // if we didn't get to end of message, we must have erred
            if (*haystack != '\0')
            {
                break;
            }

            // ensure message has no more than LimitRequestFields
            if (fields > LimitRequestFields)
            {
                break;
            }

            // valid
            return true;
        }
    }

    // invalid
    if (*message != NULL)
    {
        free(*message);
    }
    *message = NULL;
    *length = 0;
    return false;
}

/**
 * Responds to a client with status code, headers, and body of specified length.
 */
void respond(int code, const char* headers, const char* body, size_t length)
{
    // determine Status-Line's phrase
    // http://www.w3.org/Protocols/rfc2616/rfc2616-sec6.html#sec6.1
    const char* phrase = reason(code);
    if (phrase == NULL)
    {
        return;
    }

    // respond with Status-Line
    if (dprintf(cfd, "HTTP/1.1 %i %s\r\n", code, phrase) < 0)
    {
        return;
    }

    // respond with headers
    if (dprintf(cfd, "%s", headers) < 0)
    {
        return;
    }

    // respond with CRLF
    if (dprintf(cfd, "\r\n") < 0)
    {
        return;
    }

    // respond with body
    if (write(cfd, body, length) == -1)
    {
        return;
    }

    // log response line
    if (code == 200)
    {
        // green
        printf("\033[32m");
    }
    else
    {
        // red
        printf("\033[33m");
    }
    printf("HTTP/1.1 %i %s", code, phrase);
    printf("\033[39m\n");
}


/**
 * Starts server on specified port rooted at path.
 */
void start(short port, const char* path)
{
    // path to server's root
     root = realpath(path, NULL);
    // root = "../server.c";

    printf("1154 root = [%s]\n", root);
    if (root == NULL)
    {
        stop();
    }

    // ensure root is executable
    if (access(root, X_OK) == -1)
    {
        stop();
    }

    // announce root
    printf("\033[33m");
    printf("1167 Using %s for server's root", root);
    printf("\033[39m\n");

    // create a socket
    sfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sfd == -1)
    {
        stop();
    }

    // allow reuse of address (to avoid "Address already in use")
    int optval = 1;
    setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

    // assign name to socket
    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(sfd, (struct sockaddr*) &serv_addr, sizeof(serv_addr)) == -1)
    {
        printf("\033[33m");
        printf("Port %i already in use", port);
        printf("\033[39m\n");
        stop();
    }

    // listen for connections
    if (listen(sfd, SOMAXCONN) == -1)
    {
        stop();
    }

    // announce port in use
    struct sockaddr_in addr;
    socklen_t addrlen = sizeof(addr);
    if (getsockname(sfd, (struct sockaddr*) &addr, &addrlen) == -1)
    {
        stop();
    }
    printf("\033[33m");
    printf("1212 Listening on port %i", ntohs(addr.sin_port));
    printf("\033[39m\n");
}

/**
 * Stop server, deallocating any resources.
 */
void stop(void)
{
    // preserve errno across this function's library calls
    int errsv = errno;

    // announce stop
    printf("\033[33m");
    printf("Stopping server\n");
    printf("\033[39m");

    // free root, which was allocated by realpath
    if (root != NULL)
    {
        free(root);
    }

    // close server socket
    if (sfd != -1)
    {
        close(sfd);
    }

    // stop server
    exit(errsv);
}

/**
 * Transfers file at path with specified type to client.
 */
void transfer(const char* path, const char* type)
{
    // ensure path is readable
    if (access(path, R_OK) == -1)
    {
        error(403);
        return;
    }

    // open file
    FILE* file = fopen(path, "r");
    if (file == NULL)
    {
        printf("error 500 transfer failed, approx line 1171\n");
        error(500);
        return;
    }

    // load file's content
    BYTE* content;
    size_t length;
    printf("load (in transfer function approx line 1201) called\n");
    if (load(file, &content, &length) == false)
    {
        printf("error 500 transfer (which calls load) failed, approx line 1181\n");
        error(500);
        return;
    }

    // close file
    fclose(file);

    // prepare response
    char* template = "Content-Type: %s\r\n";
    printf("(printed from transfer function approx 1248)\n");
    char headers[strlen(template) - 2 + strlen(type) + 1];
    if (sprintf(headers, template, type) < 0)
    {
        printf("error 500 transfer failed, approx line 1194\n");
        error(500);
        return;
    }
    
    // respond with file's content
    respond(200, headers, content, length);

    // free file's content
    free(content);
}

/**
 * URL-decodes string, returning dynamically allocated memory for decoded string
 * that must be deallocated by caller.
 */
char* urldecode(const char* s)
{
    // check whether s is NULL
    if (s == NULL)
    {
        return NULL;
    }

    // allocate enough (zeroed) memory for an undecoded copy of s
    char* t = calloc(strlen(s) + 1, 1);
    if (t == NULL)
    {
        return NULL;
    }
    
    // iterate over characters in s, decoding percent-encoded octets, per
    // https://www.ietf.org/rfc/rfc3986.txt
    for (int i = 0, j = 0, n = strlen(s); i < n; i++, j++)
    {
        if (s[i] == '%' && i < n - 2)
        {
            char octet[3];
            octet[0] = s[i + 1];
            octet[1] = s[i + 2];
            octet[2] = '\0';
            t[j] = (char) strtol(octet, NULL, 16);
            i += 2;
        }
        else if (s[i] == '+')
        {
            t[j] = ' ';
        }
        else
        {
            t[j] = s[i];
        }
    }

    // escaped string
    return t;
}


