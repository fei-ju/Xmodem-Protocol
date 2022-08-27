
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/signal.h>
#include "xmodemserver.h"
#include "crc16.h"
#include "helper.h"

int port = 51230;

static int listenfd;
static void addclient(int fd, struct in_addr addr);
static void removeclient(int fd);

int howmany = 1;

int main(int argc, char *argv[]) {
    extern void bindandlisten(), newconnection(),activity(struct client *p);
    struct client *p;
    bindandlisten();
    fd_set fds;
    int maxfd;
    while (1) {
        maxfd = listenfd;
        FD_ZERO(&fds);
        FD_SET(listenfd, &fds);
        for (p = top; p; p = p->next) {
            FD_SET(p->fd, &fds);
            if (p->fd > maxfd)
                maxfd = p->fd;
        }
        if (select(maxfd + 1, &fds, NULL, NULL, NULL) < 0) {
            perror("select");
        } else {
//            printf("into select\n");
            for (p = top; p; p = p->next) {
                if (FD_ISSET(p->fd, &fds)) {
                    break;
                }
            }
            if(p){
//                printf("into activity\n");
//                if (p->state == initial){
                        activity(p);
//                };

            }

            if (FD_ISSET(listenfd, &fds)) {
                newconnection();
            }
        }
    }


}


void activity(struct client *p)  /* select() said activity; check it out */
{
    int len;
    char temp;
    printf("state:%d\n",(int)p->state);

    if (p->state == initial){
        printf("initial\n");
        char filename[21];
        if ((len = read(p->fd, filename, sizeof(p->filename))) < 0){
            perror("read filename");
            removeclient(p->fd);
        }

        strncat(p->buf, filename,21);
        for(int i=0; i<strlen(p->buf)-1; i++){
            if (p->buf[i]=='\r' && p->buf[i+1]=='\n'){
                printf("found new line @%d\n",i);
                p->buf[i] = '\0';
                if(i>19){
                    p->buf[19] = '\0';
                }
                strncpy(p->filename,p->buf,20);
                p->fp = open_file_in_dir(p->filename, "serverFiles");
                printf("Filename:|%s|\n",p->filename);
                if(write(p->fd, "C", 1) != 1){
                    perror("initial write C");
                }
                p->state =pre_block;
                printf("Done initial\n");
                return;
            }
        }
    }

    if (p->state == pre_block){
        printf("pre_block\n");
        for(int i=0; i<sizeof(p->buf); i++){
            p->buf[i] = SUB;
        }
        while(1){
            len = read(p->fd, &temp, 1);
            if( len <=0){
                perror("pre_block read");
                removeclient(p->fd);
                return;
            }
            if (temp == EOT){
                temp = ACK;
                if(write(p->fd, &temp, 1) != 1){
                    perror("write");
                }
                p->state = finished;
                break;
            }
            if (temp == SOH){
                p->blocksize = 128;
                p->state = get_block;
                return;
            }
            if(temp == STX){
//                printf("");
                p->blocksize = 1024;
                p->state = get_block;
                return;
            }
        }

    }
    if (p->state == get_block){
        printf("get_block\n");
        printf("p->inbuf:%d\n",p->inbuf);
        len = read(p->fd, &(p->buf[p->inbuf]), p->blocksize + 4-p->inbuf);
        if (len <=0){
            perror("get_block read");
            removeclient(p->fd);
            return;
        }
        p->inbuf += len;
        printf("read buf len: %d\n",len);
        printf("buf:%s\n",p->buf);
        printf("p->inbuf:%d\n",p->inbuf);
        if(p->inbuf == p->blocksize + 4){
            p->state = check_block;
            p->inbuf = 0;
        }

    }
    if (p->state == check_block){
        printf("check_block\n");
        unsigned char block_number = p->buf[0];
        unsigned char block_inverse = p->buf[1];
        if(block_number + block_inverse != 255){
            perror("wrong block inverse");
            removeclient(p->fd);
            return;
        }
        if(block_number == p->current_block){
            temp = ACK;
            if(write(p->fd, &temp, 1) != 1){
                perror("write");
            }
            p->state = pre_block;
            return;
        }
        if(block_number != p->current_block +1){
            perror("wrong block order\n");
            removeclient(p->fd);
            return;
        }
        unsigned char content_to_write[p->blocksize];
        for(int i=0; i<p->blocksize; i++){
            content_to_write[i] = p->buf[i+2];
        }

        unsigned short crc_result = crc_message(XMODEM_KEY,  content_to_write, p->blocksize);
        unsigned char received_crc_high = p->buf[p->blocksize + 2];
        unsigned char received_crc_low = p->buf[p->blocksize + 3];
        unsigned char calculated_high = crc_result >> 8;
        unsigned char calculated_low = crc_result;
        if (received_crc_high!=calculated_high || received_crc_low!=calculated_low){
            perror("crc incorrect");
            printf("%x",received_crc_high);
            printf("%x",calculated_high);
            printf("%x",received_crc_low);
            printf("%x",calculated_low);
            temp = NAK;
            if(write(p->fd, &temp, 1) != 1){
                perror("write");
            }
            p->state = pre_block;
            return;
        }



        if((fwrite(content_to_write, sizeof(char), p->blocksize, p->fp)) < 0){
            perror("file write");
        }
        p->current_block = (p->current_block + 1)%255;
        temp = ACK;
        if(write(p->fd, &temp, 1) != 1){
            perror("write");
        }
        p->state = pre_block;
    }
    if(p->state==finished){
        printf("finished\n");
        fclose(p->fp);
        removeclient(p->fd);
        return;
    }
    return;
}


void newconnection() {
    int fd;
    struct sockaddr_in r;
    socklen_t socklen = sizeof r;

    if ((fd = accept(listenfd, (struct sockaddr *)&r, &socklen)) < 0) {
        perror("accept");
    } else {
        addclient(fd, r.sin_addr);
    }
    return;
}


void bindandlisten()  /* bind and listen, abort on error */
{
    struct sockaddr_in r;

    if ((listenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket");
        exit(1);
    }

    memset(&r, '\0', sizeof r);
    r.sin_family = AF_INET;
    r.sin_addr.s_addr = INADDR_ANY;
    r.sin_port = htons(port);

    if (bind(listenfd, (struct sockaddr *) &r, sizeof r)) {
        perror("bind");
        exit(1);
    }

    if (listen(listenfd, 5)) {
        perror("listen");
        exit(1);
    }
}


//# From muffinman.c
static void addclient(int fd, struct in_addr addr) {
    struct client *p = malloc(sizeof(struct client));
    if (!p) {
        fprintf(stderr, "out of memory!\n");  /* highly unlikely to happen */
        exit(1);
    }
    printf("Adding client %s, total: %d\n", inet_ntoa(addr), howmany);
    fflush(stdout);
    p->fd = fd;
    p->next = top;
    p->current_block = 0;
    p->inbuf = 0;
    p->state = initial;
    p->buf[0] = '\0';
    p->filename[0] = '\0';
    top = p;
    howmany++;
}

//# From muffinman.c
static void removeclient(int fd) {
    struct client **p;
    for (p = &top; *p && (*p)->fd != fd; p = &(*p)->next);
    if (*p) {
        struct client *t = (*p)->next;
        printf("Removing client \n");
        fflush(stdout);
        free(*p);
        *p = t;
        howmany--;
    } else {
        fprintf(stderr, "Trying to remove fd %d, but I don't know about it\n", fd);
        fflush(stderr);
    }
}