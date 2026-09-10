#include "karabiner_vhid.h"

#include <errno.h>
#include <poll.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#define VHID_SOCKET "/Library/Application Support/org.pqrs/tmp/rootonly/karabiner_virtual_hid_device_service.sock"
#define VHID_PROTOCOL_VERSION 7u
#define REQ_POINTING_INIT 3u
#define REQ_POINTING_TERM 4u
#define REQ_POINTING_REPORT 11u
#define MSG_HEARTBEAT 0u
#define MSG_REQUEST 4u
#define MSG_RESPONSE 5u
#define RESP_POINTING_READY 5u
#define MAX_BODY 2048u

struct __attribute__((packed)) pointing_report {
    uint32_t buttons;
    uint8_t x;
    uint8_t y;
    uint8_t vertical_wheel;
    uint8_t horizontal_wheel;
};

static int g_fd = -1;
static uint64_t g_request_id = 1;
static bool g_enabled;
static bool g_ready;

static void put32(uint8_t p[4], uint32_t v) {
    p[0]=(uint8_t)(v>>24); p[1]=(uint8_t)(v>>16);
    p[2]=(uint8_t)(v>>8); p[3]=(uint8_t)v;
}
static void put64(uint8_t p[8], uint64_t v) {
    p[0]=(uint8_t)(v>>56); p[1]=(uint8_t)(v>>48);
    p[2]=(uint8_t)(v>>40); p[3]=(uint8_t)(v>>32);
    p[4]=(uint8_t)(v>>24); p[5]=(uint8_t)(v>>16);
    p[6]=(uint8_t)(v>>8); p[7]=(uint8_t)v;
}
static uint32_t get32(const uint8_t p[4]) {
    return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|
           ((uint32_t)p[2]<<8)|(uint32_t)p[3];
}
static uint64_t get64(const uint8_t p[8]) {
    return ((uint64_t)p[0]<<56)|((uint64_t)p[1]<<48)|
           ((uint64_t)p[2]<<40)|((uint64_t)p[3]<<32)|
           ((uint64_t)p[4]<<24)|((uint64_t)p[5]<<16)|
           ((uint64_t)p[6]<<8)|(uint64_t)p[7];
}
static bool write_all(int fd, const void *buf, size_t n) {
    const uint8_t *p=buf;
    while (n) {
        ssize_t r=write(fd,p,n);
        if (r<0) { if (errno==EINTR) continue; return false; }
        if (!r) return false;
        p+=(size_t)r; n-=(size_t)r;
    }
    return true;
}
static bool read_all(int fd, void *buf, size_t n) {
    uint8_t *p=buf;
    while (n) {
        ssize_t r=read(fd,p,n);
        if (r<0) { if (errno==EINTR) continue; return false; }
        if (!r) return false;
        p+=(size_t)r; n-=(size_t)r;
    }
    return true;
}
static bool send_frame(uint8_t type, uint64_t id,
                       const void *payload, size_t size) {
    uint8_t h[4], rid[8];
    uint32_t body=1u+8u+(uint32_t)size;
    if (g_fd<0 || body>MAX_BODY) return false;
    put32(h,body); put64(rid,id);
    return write_all(g_fd,h,sizeof(h)) &&
           write_all(g_fd,&type,1) &&
           write_all(g_fd,rid,sizeof(rid)) &&
           (!size || write_all(g_fd,payload,size));
}
static bool send_heartbeat(void) {
    uint8_t h[4];
    uint8_t type=MSG_HEARTBEAT;
    if (g_fd<0) return false;
    put32(h,1u);
    return write_all(g_fd,h,sizeof(h)) &&
           write_all(g_fd,&type,1);
}

static void parse_status(const uint8_t *p, size_t n) {
    size_t i;
    for (i=0;i+1<n;i+=2)
        if (p[i]==RESP_POINTING_READY) g_ready=p[i+1]!=0;
}
static bool receive_response(uint64_t wanted, bool require_ready) {
    int timeout_ms=3000;
    for (;;) {
        struct pollfd pfd={.fd=g_fd,.events=POLLIN};
        uint8_t h[4], body[MAX_BODY];
        uint32_t n;
        int pr;
        do { pr=poll(&pfd,1,timeout_ms); } while (pr<0 && errno==EINTR);
        if (pr<=0 || !read_all(g_fd,h,sizeof(h))) return false;
        n=get32(h);
        if (!n || n>sizeof(body) || !read_all(g_fd,body,n)) return false;
        if (body[0]==MSG_HEARTBEAT) continue;
        if ((body[0]==MSG_REQUEST || body[0]==MSG_RESPONSE) && n>=9) {
            uint64_t id=get64(body+1);
            const uint8_t *payload=body+9;
            size_t payload_size=n-9;
            parse_status(payload,payload_size);
            if (body[0]==MSG_REQUEST) {
                if (!send_frame(MSG_RESPONSE,id,NULL,0)) return false;
                if (require_ready && g_ready) return true;
                continue;
            }
            if (id==wanted && (!require_ready || g_ready)) return true;
        }
    }
}
static bool send_request(uint8_t request,
                         const void *data, size_t size,
                         bool require_ready) {
    uint8_t payload[3+sizeof(struct pointing_report)];
    uint16_t version=VHID_PROTOCOL_VERSION;
    uint64_t id=g_request_id++;
    if (size>sizeof(payload)-3) return false;
    memcpy(payload,&version,2);
    payload[2]=request;
    if (size) memcpy(payload+3,data,size);
    return send_frame(MSG_REQUEST,id,payload,3+size) &&
           receive_response(id,require_ready);
}
static void disconnect_socket(void) {
    if (g_fd>=0) close(g_fd);
    g_fd=-1; g_ready=false;
}

int tpsc_vhid_initialize(void) {
    struct sockaddr_un addr;
    if (g_enabled && g_fd>=0 && g_ready) return 0;
    disconnect_socket();
    g_fd=socket(AF_UNIX,SOCK_STREAM,0);
    if (g_fd<0) { perror("trackpoint: virtual HID socket"); return -1; }
    memset(&addr,0,sizeof(addr));
    addr.sun_family=AF_UNIX;
    if (strlen(VHID_SOCKET)>=sizeof(addr.sun_path)) {
        fprintf(stderr,"trackpoint: Karabiner virtual HID socket path too long\n");
        disconnect_socket(); return -1;
    }
    memcpy(addr.sun_path,VHID_SOCKET,strlen(VHID_SOCKET)+1);
    if (connect(g_fd,(struct sockaddr *)&addr,sizeof(addr))!=0) {
        fprintf(stderr,"trackpoint: cannot connect to Karabiner virtual HID daemon: %s\n",strerror(errno));
        fprintf(stderr,"trackpoint: --karabiner-vhid must run as root and requires Karabiner's virtual HID daemon\n");
        disconnect_socket(); return -1;
    }
    g_enabled=true;
    if (!send_request(REQ_POINTING_INIT,NULL,0,true)) {
        fprintf(stderr,"trackpoint: Karabiner virtual pointing device did not become ready\n");
        g_enabled=false; disconnect_socket(); return -1;
    }
    fprintf(stderr,"trackpoint: Karabiner virtual HID pointing device ready\n");
    return 0;
}

void tpsc_vhid_shutdown(void) {
    if (g_fd>=0 && g_enabled)
        (void)send_request(REQ_POINTING_TERM,NULL,0,false);
    g_enabled=false; disconnect_socket();
}
bool tpsc_vhid_is_enabled(void) {
    return g_enabled && g_fd>=0 && g_ready;
}

bool tpsc_vhid_keepalive(void) {
    if (!tpsc_vhid_is_enabled()) {
        return tpsc_vhid_initialize() == 0;
    }

    if (send_heartbeat())
        return true;

    fprintf(stderr,
            "trackpoint: Karabiner virtual HID heartbeat failed; reconnecting\n");
    g_enabled=false;
    disconnect_socket();
    return tpsc_vhid_initialize() == 0;
}
static int8_t take_chunk(int64_t *v) {
    int64_t c=*v;
    if (c>127) c=127;
    else if (c<-127) c=-127;
    *v-=c;
    return (int8_t)c;
}
static bool post_pointing_once(uint32_t buttons, int64_t dx, int64_t dy) {
    bool first=true;

    do {
        struct pointing_report report={0};
        report.buttons=buttons;
        report.x=(uint8_t)take_chunk(&dx);
        report.y=(uint8_t)take_chunk(&dy);
        if (!send_request(REQ_POINTING_REPORT,&report,sizeof(report),false))
            return false;
        first=false;
    } while (dx || dy);

    (void)first;
    return true;
}

bool tpsc_vhid_post_pointing(uint32_t buttons, int64_t dx, int64_t dy) {
    int64_t original_dx=dx;
    int64_t original_dy=dy;

    if (!tpsc_vhid_is_enabled() && tpsc_vhid_initialize()!=0)
        return false;

    if (post_pointing_once(buttons,dx,dy))
        return true;

    fprintf(stderr,
            "trackpoint: lost Karabiner virtual HID connection; reconnecting\n");
    g_enabled=false;
    disconnect_socket();

    if (tpsc_vhid_initialize()!=0)
        return false;

    return post_pointing_once(buttons,original_dx,original_dy);
}

bool tpsc_vhid_post_relative(int64_t dx, int64_t dy) {
    if (dx == 0 && dy == 0)
        return true;
    return tpsc_vhid_post_pointing(0,dx,dy);
}
