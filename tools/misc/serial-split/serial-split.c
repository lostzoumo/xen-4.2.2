/*
 *  serial-split.c
 *  pdb / console splitter
 *
 *  Copyright 2005 Charles Coffing <ccoffing@novell.com>
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version 2
 *  of the License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 */

/*
 *  Typical setup:
 *
 *               Development box          Xen box
 *                      ...-----+        +-----...
 *  +---------+                 |        |
 *  | gdb     |                 |        |
 *  |         |\ high           |        |
 *  +---------+ \               |        |
 *               \+-----------+ | serial | +------------------+
 *                |  splitter |------------| Xen              |
 *               /+-----------+ |        | |  - pdb    (com1H)|
 *  +---------+ /               |        | |  - printk (com1) |
 *  | console |/ low            |        | +------------------+
 *  | viewer  |                 |        |
 *  +---------+                 |        |
 *                      ...-----+        +-----...
 */


#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <sys/signal.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>

const unsigned int DefaultLowPort = 12010;
const unsigned int DefaultBaud = 115200;
const char DefaultSerialDevice[] = "/dev/ttyS0";

#define DEBUG 0
#define MAX(a,b) ((a)<(b)?(b):(a))


static int cook_baud(int baud)
{
    int cooked_baud = 0;
    switch (baud)
    {
    case     50: cooked_baud =     B50; break;
    case     75: cooked_baud =     B75; break;
    case    110: cooked_baud =    B110; break;
    case    134: cooked_baud =    B134; break;
    case    150: cooked_baud =    B150; break;
    case    200: cooked_baud =    B200; break;
    case    300: cooked_baud =    B300; break;
    case    600: cooked_baud =    B600; break;
    case   1200: cooked_baud =   B1200; break;
    case   1800: cooked_baud =   B1800; break;
    case   2400: cooked_baud =   B2400; break;
    case   4800: cooked_baud =   B4800; break;
    case   9600: cooked_baud =   B9600; break;
    case  19200: cooked_baud =  B19200; break;
    case  38400: cooked_baud =  B38400; break;
    case  57600: cooked_baud =  B57600; break;
    case 115200: cooked_baud = B115200; break;
    }
    return cooked_baud;
}


static int start_listener(unsigned short port)
{
    int fd;
    struct sockaddr_in sin;
    int on = 1;

    if ((fd = socket (AF_INET, SOCK_STREAM, 0)) < 0)
    {
        perror("socket");
        goto out1;
    }

    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof (on));

    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_port = htons (port);
    sin.sin_addr.s_addr = INADDR_ANY;
    if (bind(fd, (struct sockaddr *)&sin, sizeof(sin)) < 0)
    {
        perror("bind");
        goto out2;
    }

    if (listen(fd, 1) < 0)
    {
        perror("listen");
        goto out2;
    }

    fprintf(stderr, "Listening on port %d\n", port);

    return fd;

out2:
    close(fd);
out1:
    return -1;
}


static int accept_conn(int fd)
{
    int on = 1;
    int new_fd;
    struct sockaddr_in from;
    socklen_t fromlen = sizeof(from);

    new_fd = accept(fd, (struct sockaddr *)&from, &fromlen);
    if (new_fd < 0)
        perror("accept");
    ioctl(new_fd, FIONBIO, &on);

    fprintf(stderr, "Accepted connection on %d\n", new_fd);

    return new_fd;
}


static void close_conn(int * fd)
{
    shutdown(*fd, 2);
    close(*fd);
    *fd = -1;
}


static int receive_data(int * fd, char * buf, ssize_t max_bytes, int * poll)
{
    ssize_t bytes;
    if ((bytes = read(*fd, buf, max_bytes)) < 0)
    {
        perror("read");
        *poll = 1;
        return 0;
    }
    else if (bytes == 0)
    {
        close_conn(fd);
        *poll = 0;
        return 0;
    }
    else
    {
        if (bytes == max_bytes)
            *poll = 1;
        else
            *poll = 0;
#if DEBUG
        {
            ssize_t i;
            fprintf(stderr, "Received %d bytes on %d:\n", bytes, *fd);
            for (i = 0; i < bytes; ++ i)
            {
                if ((i & 0xf) == 0)
                    printf("    ");
                printf("%02x", buf[i] & 0xff);
                if (((i+1) & 0xf) == 0 || i + 1 == bytes)
                    printf("\n");
                else
                    printf(" ");
            }
        }
#endif
        return bytes;
    }
}


static void set_high_bit(char * buf, size_t bytes)
{
    size_t i;
    for(i = 0; i < bytes; ++ i)
        buf[i] |= 0x80;
}


static void clear_high_bit(char * buf, size_t bytes)
{
    size_t i;
    for(i = 0; i < bytes; ++ i)
        buf[i] &= 0x7f;
}


static int open_serial(char const * serial_dev, int baud)
{
    struct termios newsertio;
    int serial_fd;
    memset(&newsertio, 0, sizeof(newsertio));

    if ((serial_fd = open(serial_dev, O_RDWR | O_NOCTTY | O_NONBLOCK)) < 0)
    {
        perror(serial_dev);
        return -1;
    }

    newsertio.c_cflag = baud | CS8 | CLOCAL | CREAD;
    newsertio.c_iflag = IGNBRK | IGNPAR;    /* raw input */
    newsertio.c_oflag = 0;                  /* raw output */
    newsertio.c_lflag = 0;                  /* no echo, no signals */
    newsertio.c_cc[VMIN] = 1;
    newsertio.c_cc[VTIME] = 0;
    tcflush(serial_fd, TCIFLUSH);
    tcsetattr(serial_fd, TCSANOW, &newsertio);

    fprintf(stderr, "Listening on %s\n", serial_dev);

    return serial_fd;
}


static void main_loop(int serial_fd, int low_listener, int high_listener)
{
    fd_set rdfds;
    int low_poll = 0, high_poll = 0, serial_poll = 0;
    int low_fd = -1, high_fd = -1;

    while(1)
    {
        char buf[1024];
        ssize_t bytes;
        int max;

        FD_ZERO(&rdfds);
        FD_SET(low_fd < 0 ? low_listener : low_fd, &rdfds);
        FD_SET(high_fd < 0 ? high_listener : high_fd, &rdfds);
        FD_SET(serial_fd, &rdfds);

        max = MAX(low_fd, low_listener);
        max = MAX(max, high_fd);
        max = MAX(max, high_listener);
        max = MAX(max, serial_fd);

        if (select(max + 1, &rdfds, NULL, NULL, NULL) < 0)
        {
            perror("select");
            continue;
        }

        if (FD_ISSET(low_listener, &rdfds))
        {
            assert(low_fd < 0);
            low_fd = accept_conn(low_listener);
        }

        if (FD_ISSET(high_listener, &rdfds))
        {
            assert(high_fd < 0);
            high_fd = accept_conn(high_listener);
        }

        if (low_poll || (low_fd >= 0 && FD_ISSET(low_fd, &rdfds)))
        {
            if ((bytes = receive_data(&low_fd, &buf[0], sizeof(buf),
                                      &low_poll)) > 0)
            {
                clear_high_bit(&buf[0], bytes);
                if (write(serial_fd, &buf[0], bytes) < 0)
                    perror("write");
            }
        }

        if (high_poll || (high_fd >= 0 && FD_ISSET(high_fd, &rdfds)))
        {
            if ((bytes = receive_data(&high_fd, &buf[0], sizeof(buf),
                                      &high_poll)) > 0)
            {
                set_high_bit(&buf[0], bytes);
                if (write(serial_fd, &buf[0], bytes) < 0)
                    perror("write");
            }
        }

        if (serial_poll || FD_ISSET(serial_fd, &rdfds))
        {
            if ((bytes = receive_data(&serial_fd, &buf[0], sizeof(buf),
                                      &serial_poll)) > 0)
            {
                ssize_t i;
                for (i = 0; i < bytes; ++ i)
                {
                    if (buf[i] & 0x80)
                    {
                        if (high_fd >= 0)
                        {
                            buf[i] &= 0x7f;
                            if ((write(high_fd, &buf[i], 1)) < 0)
                            {
                                perror("write");
                                close_conn(&high_fd);
                                high_poll = 0;
                            }
                        }
                    }
                    else
                    {
                        if (low_fd >= 0)
                        {
                            if ((write(low_fd, &buf[i], 1)) < 0)
                            {
                                perror("write");
                                close_conn(&low_fd);
                                low_poll = 0;
                            }
                        }
                    }
                }
            }
        }
    }
}


static void usage()
{
    printf(
"Description:\n"
"        Splits the serial port between two TCP ports.  Bytes read from the\n"
"        serial port will be delivered to one of the two TCP ports (high or\n"
"        low) depending on whether the high bit is set.  Bytes written to the\n"
"        TCP ports will be forwarded to the serial port; the high bit will be\n"
"        set or cleared to denote the source.\n"
"Usage:\n"
"        serial-split [-d<serial-device>] [-b<baud>]\n"
"                     [-l<low-port>] [-h<high-port>]\n"
"Parameters:\n"
"        -d<serial-device>  Defaults to %s.\n"
"        -b<baud>           Baud rate of the serial port.  Defaults to %d.\n"
"                           Also assumes 8N1.\n"
"        -l<low-port>       Low TCP port.  Defaults to %d, or one less than\n"
"                           the high port.\n"
"        -h<high-port>      High TCP port.  Defaults to %d, or one more than\n"
"                           the low port.\n",
DefaultSerialDevice, DefaultBaud, DefaultLowPort, DefaultLowPort + 1);

    exit(1);
}


int main(int argc, char **argv)
{
    int cooked_baud = cook_baud(DefaultBaud);
    char const * serial_dev = DefaultSerialDevice;
    int low_port = -1, high_port = -1;
    int serial_fd, low_listener, high_listener;

    while ( --argc != 0 )
    {
        char *p = argv[argc];
        if ( *(p++) != '-' )
            usage();
        switch (*(p++))
        {
        case 'b':
            if ( (cooked_baud = cook_baud(atoi(p))) == 0 )
            {
                fprintf(stderr, "Bad baud rate\n");
                exit(1);
            }
            break;
        case 'd':
            serial_dev = p;
            break;
        case 'l':
            if ((low_port = atoi(p)) <= 0)
                usage();
            break;
        case 'h':
            if ((high_port = atoi(p)) <= 0)
                usage();
            break;
        default:
            usage();
        }
    }

    if (low_port == -1 && high_port == -1)
        low_port = DefaultLowPort;
    if (low_port == -1)
        low_port = high_port - 1;
    if (high_port == -1)
        high_port = low_port + 1;

    if ((serial_fd = open_serial(serial_dev, cooked_baud)) < 0 ||
        (low_listener = start_listener(low_port)) < 0 ||
        (high_listener = start_listener(high_port)) < 0)
        exit(1);

    main_loop(serial_fd, low_listener, high_listener);

    return 0;
}

