#include <unistd.h>
#include <string>
#include <sstream>
#include <fstream>
#include <netdb.h>
#include <cstring>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sqlite3.h>
#include <regex.h>
#include <vector>
using namespace std;
static const char dbname[] = "cache.db";
static const char httpfrbd[] = "HTTP/1.1 403 Forbidden\r\nConnection: close\r\nContent-Length: 33\r\n\r\nThis website has been blacklisted";

int newWebSock(string &srvURL) {
    //This function establishes a connection to the host specified in 'srvrURL'
    //It returns the new open socket, or some value less than 1 on error
    struct addrinfo *dnsresp_root, *dnsresp, dnshints;
	memset(&dnshints, 0, sizeof(struct addrinfo)); 
    dnshints.ai_family = AF_UNSPEC;
    dnshints.ai_socktype = SOCK_STREAM;

    int sock;
    //Return 0 if getaddrinfo encounters an error
    if (getaddrinfo(srvURL.c_str(), "http", &dnshints, &dnsresp_root)) return 0;

    //For each possible connection...
    for (dnsresp = dnsresp_root; dnsresp != NULL; dnsresp = dnsresp->ai_next) {
        sock = socket(dnsresp->ai_family, dnsresp->ai_socktype, dnsresp->ai_protocol);
        if (sock < 1) {
            //If socket failed, retry
            sock = 0;
            continue;
        } else if (connect(sock, dnsresp->ai_addr, dnsresp->ai_addrlen)) {
            //If connection failed, retry
            close(sock);
            sock = 0;
            continue;
        } else break;
    }

    freeaddrinfo(dnsresp_root);
    return sock;
}

int sql_cb(void *void_args, int colCount, char **colVals, char **colNames) {
    //This function is called when searching the SQL cache table for
    //last-modified dates. It's only called if there was a match for the
    //given URL in the table. When called, it fills in the 'cachedDate'
    //pointer with that URL's corresponding last-modified date
    char *cachedDate = (char *) void_args;
    strcpy(cachedDate, colVals[0]);
    return 0;
}

void filterProfanity(string &buffer) {
    int sa; //Segment A: Index of first char in non-tag segment
    int sb; //Segment B: Index of first char after non-tag segment
    int ma; //Match A: Index of first char in word match
    int mb; //Match B: Index of first char after word match
    int mc; //Match C: Index of search offset in non-tag segment
    int buflen = buffer.length();
    vector<string> words;

    ifstream file("profanity.txt");
    if (file.is_open()) {
        string word;
        while (!file.eof()) {
            file >> word;
            words.push_back(word);
        }
        file.close();
    }

    sb = 0;
    while (sb < buflen) {
        //If at the beginning of an html tag...
        if (buffer[sb] == '<') {
            //Find the end of the tag...
            while (buffer[sb] != '>') if (sb == buflen) break; else sb++;
            //Set sa to the first character after the tag...
            sa = ++sb;
        } else sa = sb;

        //Find end of current non-tag segment...
        while (sb < buflen && buffer[sb] != '<') sb++;

        //If segment contains anything...
        if (sb-sa > 0) {
            //For each word...
            for (int w = 0; w < words.size(); w++) {
                mc = sa;
                //While matches are found for this word...
                while ((ma = buffer.substr(mc, sb-mc).find(words[w])) != string::npos) {
                    ma += mc;
                    mb = ma + words[w].length();
                    //If ma is a valid beginning and mb is a valid end...
                    if ((ma == mc || !isalnum(buffer[ma-1])) && (mb >= buflen || !isalnum(buffer[mb])))
                        buffer.replace(ma, words[w].length(), words[w].length(), 'X');
                    mc = mb;
    }   }   }   }
}

void extractURL(int sock_client, string &srvrURL, string &path) {
    //This function verifies that a client request was for an actual webpage
    //If so, it fills 'srvrURL' with the requested web address
    //Otherwise, it leaves 'srvrURL' empty
    regex_t REG_GET;
    regmatch_t reg_matches[9];
    char *buffer = (char *)malloc(1024);
    srvrURL.clear();

    regcomp(&REG_GET, "GET /?(([[:alnum:]]+\\.)+[[:alnum:]]+\\.((com)|(net)|(edu)|(org)))(/.*)? HTTP/1\\.1", REG_EXTENDED);
    int rcv = recv(sock_client, buffer, 1023, 0);
    buffer[rcv] = '\0';
    if (rcv == 0 || regexec(&REG_GET, buffer, 9, reg_matches, 0)) return;
    
    srvrURL.assign(&buffer[reg_matches[1].rm_so], reg_matches[1].rm_eo - reg_matches[1].rm_so);
    if (reg_matches[8].rm_so == -1) path.assign("/");
    else path.assign(&buffer[reg_matches[8].rm_so], reg_matches[8].rm_eo - reg_matches[8].rm_so);
    free(buffer);
}

void recvHTTP(int webSock, int &code, string &page) {
    //This function gets the response from a web server attached to 'webSock'
    //'page' is filled with the response, using 'buffer' as an intermediary
    //'code' is set to the HTTP response code in the server's message or -1
    //if the connection is lost
    //'recv()' is passed the MSG_WAITALL option to force it to get the WHOLE
    //message or else to fill the WHOLE buffer in one call
    char *buffer = (char *)malloc(100000);
    page.clear();

    int rcv = recv(webSock, buffer, 100000, MSG_WAITALL);
    if (rcv == 0) { code = -1; return; }
    code = stoi(&buffer[9], NULL, 10);
    page.assign(buffer, rcv);

    //If the buffer was filled up, repeat till its not
    while (rcv == 100000) {
        rcv = recv(webSock, buffer, 100000, MSG_WAITALL);
        page.append(buffer, rcv);
    }
    free(buffer);
}

void updateCache(string &page, string &srvrURL, string &lastMod) {
    //This function inserts updated last-modified dates into the cache table
    //and writes out new cache pages to the working directory
    sqlite3 *db;
    string sql;
    sql.assign("INSERT OR REPLACE INTO Cache VALUES ('" + srvrURL + "', '" + lastMod + "')");
    sqlite3_open(dbname, &db);
    sqlite3_exec(db, sql.c_str(), NULL, NULL, NULL);
    sqlite3_close(db);
    ofstream file(srvrURL);
    file.write(page.c_str(), page.length());
    file.close();
}

void removeChunks(string &page) {
    //This function removes the chunk data from a newly arrived
    //HTTP message sent using the transfer-encoding chunked mode
    int chunkbeg = page.find("\r\n\r\n") + 4;
    int chunkend = page.find("\r\n", chunkbeg) + 2;
    int chunklen = chunkend - chunkbeg;
    int chunk = stoi(page.substr(chunkbeg, chunklen), NULL, 16);
    while (chunk) {
        page.replace(chunkbeg, chunklen, "");
        chunkbeg += chunk;
        chunkend = page.find("\r\n", chunkbeg+2) + 2;
        chunklen = chunkend - chunkbeg;
        chunk = stoi(page.substr(chunkbeg, chunklen), NULL, 16);
    }
    page.replace(chunkbeg, chunklen, "");
}

void alterPage(string &page, string &srvrURL) {
    //This function takes in a newly arrived HTTP response, 
    //removes its header and chunk data (if it exists),
    //inserts a 'base' html element into the page,
    //filters profanity, adds our own header to the page,
    //and stores it in the cache
    int headend = page.find("\r\n\r\n") + 4;
    int contlen = page.substr(0, headend).find("Content-Length");
    int transenc = page.substr(0, headend).find("Transfer-Encoding");
    int lastmod = page.substr(0, headend).find("Last-Modified");
    stringstream header;
    string base("<base href=\"http://" + srvrURL + "\">");
    string date;

    if (lastmod != string::npos) date.assign(page, lastmod+15, 29);
    if (contlen == string::npos && transenc != string::npos) removeChunks(page);
    int headelm = page.find("<head>", headend);
    if (headelm != string::npos) page.insert(headelm + 6, base);

    header << "HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Length: " << page.length()-headend << "\r\n\r\n";
    page.replace(0, headend, header.str());
    filterProfanity(page);

    //Only store to cache if the page had a last-modified feild
    if (lastmod != string::npos) updateCache(page, srvrURL, date);
}

void sendPage(int sock_client, string &page) {
    int rcv = 0, off = 0, rem = page.length();
    while (rem -= rcv) {
        rcv += send(sock_client, &page[off], rem, 0);
        off += rcv;
    }
}

string readFile(string &filename) {
    //This function reads a file, returning it as a string
    ifstream in(filename, ios::in | ios::binary);
    string contents;
    in.seekg(0, ios::end);
    contents.resize(in.tellg());
    in.seekg(0, ios::beg);
    in.read(&contents[0], contents.size());
    in.close();
    return(contents);
}

int fetchPage(string &page, string &srvrURL, char cachedDate[32]) {
    //This function is called if the webpage is cached
    //It sends an If-Modified request to the parent server
    //If the cached page is still good, it just reads it from cache
    //If not, it downloads a new one and overwrites the cache
    string request;
    int code, webSock;

    webSock = newWebSock(srvrURL);
    if (webSock < 1) return -1;

    request.assign("GET / HTTP/1.1\r\nHost: " + srvrURL + "\r\nConnection: close\r\nIf-Modified-Since: " + cachedDate + "\r\n\r\n");
    int ret = send(webSock, request.c_str(), request.length(), 0);

    if (ret < request.length()) {
        close(webSock);
        return -1;
    }

    recvHTTP(webSock, code, page);
    switch (code) {
        case 304: page.assign(readFile(srvrURL)); break;
        case 200: alterPage(page, srvrURL); break;
        case  -1: close(webSock); return -1;
    }

    close(webSock);
    return 0;
}

int downloadPage(string &page, string &srvrURL) {
    //This function is called if the webpage is not cached
    //It sends a request to the webserver and stores a copy
    //of the page into cache
    string request;
    int ret, code, webSock;

    webSock = newWebSock(srvrURL);
    if (webSock < 1) return -1;

    request.assign("GET / HTTP/1.1\r\nHost: " + srvrURL + "\r\nConnection: close\r\n\r\n");
    ret = send(webSock, request.c_str(), request.length(), 0);

    if (ret < request.length()) {
        close(webSock);
        return -1;
    }

    recvHTTP(webSock, code, page);
    switch (code) {
        case 200: alterPage(page, srvrURL); break;
        case  -1: close(webSock); return -1;
    }

    close(webSock);
    return 0;
}

void *client_handler(void *void_args) {
    string srvrURL, sql, page, path;
    char cachedDate[32] = {'\0'};
    sqlite3 *db = NULL;
    int err = 0, sock_client = *(int*) void_args;

    free(void_args);
    extractURL(sock_client, srvrURL, path);

    if (srvrURL.length() == 0) {
        //Then the request was not for a web page
        close(sock_client);
        return NULL;
    } else {
        printf("Got request for: %s%s\n", srvrURL.c_str(), path.c_str());
    }

    //Check the cache database for the requested URL. If the page has
    //an entry there, 'cachedDate' will be filled with its last-modified
    //value, or with 'BLACKLISTED' if the page has been blacklisted
    sql.assign("SELECT date FROM Cache WHERE url = '" + srvrURL + "'");
    sqlite3_open(dbname, &db);
    sqlite3_exec(db, sql.c_str(), sql_cb, (void *) &cachedDate, NULL);
    sqlite3_close(db);

    if (strlen(cachedDate) == 0) {
        //Then the page doesn't exist in the cache
        err = downloadPage(page, srvrURL);
    } else if (!strcmp(cachedDate, "BLACKLISTED")) {
        page.assign(httpfrbd);
    } else {
        //Then the page does exist in the cache
        err = fetchPage(page, srvrURL, cachedDate);
    }

    if (err) {
        close(sock_client);
        return NULL;
    }

    sendPage(sock_client, page);
    system("sleep 1");
    close(sock_client);
    return NULL;
}