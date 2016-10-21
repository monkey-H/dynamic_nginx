#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

typedef struct {
    struct sockaddr                *sockaddr;
    socklen_t                       socklen;
    ngx_str_t                       name;
    ngx_uint_t                      down;
    ngx_int_t                       weight;
} ngx_http_upstream_dynamic_hash_peer_t;

typedef struct {
    ngx_uint_t                        number;
    ngx_uint_t                        total_weight;
    unsigned                          weighted:1;
    ngx_http_upstream_dynamic_hash_peer_t     peer[0];
} ngx_http_upstream_dynamic_hash_peers_t;

typedef struct {
    ngx_http_upstream_dynamic_hash_peers_t     *peers;

    ngx_uint_t                         hash;

    u_char                             addr[3];

    u_char                             tries;

    ngx_event_get_peer_pt              get_rr_peer;
} ngx_http_upstream_dynamic_hash_peer_data_t;

static ngx_int_t ngx_http_upstream_init_dynamic_hash_peer(ngx_http_request_t *r,
                                                          ngx_http_upstream_srv_conf_t *us);
static ngx_int_t ngx_http_upstream_get_dynamic_hash_peer(ngx_peer_connection_t *pc,
                                                         void *data);
static char *ngx_http_upstream_dynamic_hash(ngx_conf_t *cf, ngx_command_t *cmd,
                                            void *conf);

static int h1(char* str, int len);
static int h2(char* str, int len);
static void getPermutation(int** permutation, int m, int n, char** name);
static void init_peers(int row, int col, int* weight, char** name, int* entry);
static void print_sockaddr(ngx_log_t *log, struct sockaddr *ip);

static ngx_command_t  ngx_http_upstream_dynamic_hash_commands[] = {

        { ngx_string("dynamic_hash"),
          NGX_HTTP_UPS_CONF|NGX_CONF_NOARGS,
          ngx_http_upstream_dynamic_hash,
          0,
          0,
          NULL },

        ngx_null_command
};


static ngx_http_module_t  ngx_http_upstream_dynamic_hash_module_ctx = {
        NULL,                                  /* preconfiguration */
        NULL,                                  /* postconfiguration */

        NULL,                                  /* create main configuration */
        NULL,                                  /* init main configuration */

        NULL,                                  /* create server configuration */
        NULL,                                  /* merge server configuration */

        NULL,                                  /* create location configuration */
        NULL                                   /* merge location configuration */
};


ngx_module_t  ngx_http_upstream_dynamic_hash_module = {
        NGX_MODULE_V1,
        &ngx_http_upstream_dynamic_hash_module_ctx, /* module context */
        ngx_http_upstream_dynamic_hash_commands,    /* module directives */
        NGX_HTTP_MODULE,                       /* module type */
        NULL,                                  /* init master */
        NULL,                                  /* init module */
        NULL,                                  /* init process */
        NULL,                                  /* init thread */
        NULL,                                  /* exit thread */
        NULL,                                  /* exit process */
        NULL,                                  /* exit master */
        NGX_MODULE_V1_PADDING
};


ngx_int_t
ngx_http_upstream_init_dynamic_hash(ngx_conf_t *cf, ngx_http_upstream_srv_conf_t *us)
{
    ngx_http_upstream_server_t      *server;
    char**                          server_name;
    int*                            weight;
    struct sockaddr_in              *ip;
    char                            *addr_name;
    char                            *ip_addr;
    unsigned int                    ip_port;
    int                             count;
    int                             server_num;
    int*                            entry;
    ngx_uint_t                      col=7;
    ngx_uint_t                      i;
    ngx_http_upstream_dynamic_hash_peers_t *peers;

    fprintf(stderr, "dynamic func %s\n", "init");
    us->peer.init = ngx_http_upstream_init_dynamic_hash_peer;

    server = us->servers->elts;

    server_num=0;
    for (i=0; i<us->servers->nelts; i++) {
        if (server[i].backup)
            continue;

        server_num++;
    }

    if (server_num == 0) {
        return NGX_ERROR;
    }

    server_name = (char **)malloc(sizeof(char *) * server_num);
    weight = (int *)malloc(sizeof(int) * server_num);

    count = 0;
    for (i = 0; i < us->servers->nelts; i++) {
        if (server[i].backup)
            continue;

        ip = (struct sockaddr_in *)server[i].addrs[0].sockaddr;
        ip_addr = inet_ntoa(ip->sin_addr);
        ip_port = (unsigned int)ntohs(ip->sin_port);

        addr_name = (char *)malloc(sizeof(char) * 30);

        sprintf(addr_name, "%d", ip_port);
        strcat(addr_name, ip_addr);
	fprintf(stderr, "addr name %s\n", addr_name);
        server_name[count] = addr_name;
        weight[count] = server[i].weight;
        fprintf(stderr, "addr weight %d\n", (int)server[i].weight);
        count ++;

    }

    entry = (int*)malloc(sizeof(int) * col);
    init_peers(server_num, col, weight, server_name, entry);

    peers = ngx_pcalloc(cf->pool, sizeof(ngx_http_upstream_dynamic_hash_peers_t)
                                  + sizeof(ngx_http_upstream_dynamic_hash_peer_t) * server_num);

    for (i=0; i<col; i++) {
	ngx_log_stderr(0, "dynamic: %d: name: \"%s\"", i, inet_ntoa(((struct sockaddr_in *)server[entry[i]].addrs[0].sockaddr)->sin_addr));
	ngx_log_stderr(0, "dynamic: %d: port: \"%d\"", i, (unsigned int)ntohs(((struct sockaddr_in *)server[entry[i]].addrs[0].sockaddr)->sin_port));
        peers->peer[i].sockaddr = server[entry[i]].addrs[0].sockaddr;
        peers->peer[i].socklen = server[entry[i]].addrs[0].socklen;
        peers->peer[i].name = server[entry[i]].addrs[0].name;
        peers->peer[i].down = server[entry[i]].down;
        peers->peer[i].weight = server[entry[i]].weight;
    }

    us->peer.data = peers;

    return NGX_OK;
}

static ngx_int_t
ngx_http_upstream_init_dynamic_hash_peer(ngx_http_request_t *r,
                                         ngx_http_upstream_srv_conf_t *us)
{
    char                                 name[30];
    struct sockaddr_in                     *sin;
    ngx_http_upstream_dynamic_hash_peer_data_t  *iphp;

    fprintf(stderr, "dynamic func %s\n", "init peer");

    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "dynamic point at init peer %s", "hello"
                   );

    iphp = ngx_palloc(r->pool, sizeof(ngx_http_upstream_dynamic_hash_peer_data_t));
    if (iphp == NULL) {
        return NGX_ERROR;
    }

    r->upstream->peer.data = iphp;

    iphp->peers = us->peer.data;

    r->upstream->peer.get = ngx_http_upstream_get_dynamic_hash_peer;

    sin = (struct sockaddr_in *) r->connection->sockaddr;
    strcat(name, inet_ntoa(sin->sin_addr));

    iphp->hash = h1(name, strlen(name)) % 7;

    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "dynamic client name %s", name
                   );
    
    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "dynamic iphp hash %d",
                   iphp->hash);

    iphp->tries = 0;
    //iphp->get_rr_peer = ngx_http_upstream_get_round_robin_peer;

    return NGX_OK;
}


static ngx_int_t
ngx_http_upstream_get_dynamic_hash_peer(ngx_peer_connection_t *pc, void *data)
{
    ngx_http_upstream_dynamic_hash_peer_data_t  *iphp = data;

    ngx_uint_t                    hash;
    ngx_http_upstream_dynamic_hash_peer_t  *peer;

    fprintf(stderr, "dynamic func %s\n", "get peer");

    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, pc->log, 0,
                   "dynamic get ip hash peer, try: %ui", pc->tries);

//    if (iphp->tries > 20 || iphp->rrp.peers->single) {
//        return iphp->get_rr_peer(pc, &iphp->rrp);
//    }
//
//    now = ngx_time();

    pc->cached = 0;
    pc->connection = NULL;

    hash = iphp->hash;

//    for ( ;; ) {
//
//        p = hash % iphp->rrp.peers->number;
//
//        n = p / (8 * sizeof(uintptr_t));
//        m = (uintptr_t) 1 << p % (8 * sizeof(uintptr_t));
//
//        if (!(iphp->rrp.tried[n] & m)) {
//
//            ngx_log_debug2(NGX_LOG_DEBUG_HTTP, pc->log, 0,
//                           "get ip hash peer, hash: %ui %04XA", p, m);
//
//            peer = &iphp->rrp.peers->peer[hash];
//
//            /* ngx_lock_mutex(iphp->rrp.peers->mutex); */
//
//            if (!peer->down) {
//
//                if (peer->max_fails == 0 || peer->fails < peer->max_fails) {
//                    break;
//                }
//
//                if (now - peer->accessed > peer->fail_timeout) {
//                    peer->fails = 0;
//                    break;
//                }
//            }
//
//            iphp->rrp.tried[n] |= m;
//
//            /* ngx_unlock_mutex(iphp->rrp.peers->mutex); */
//
//            pc->tries--;
//        }
//
//        if (++iphp->tries >= 20) {
//            return iphp->get_rr_peer(pc, &iphp->rrp);
//        }
//    }

    peer = &iphp->peers->peer[hash];

//    iphp->rrp.current = p;

    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, pc->log, 0,
                   "dynamic get peer: %d",
                   hash);
    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, pc->log, 0, "dynamic sockaddr: %s", "world");

    print_sockaddr(pc->log, peer->sockaddr);

    pc->sockaddr = peer->sockaddr;
    pc->socklen = peer->socklen;
    pc->name = &peer->name;

    /* ngx_unlock_mutex(iphp->rrp.peers->mutex); */

//    iphp->rrp.tried[n] |= m;
    iphp->hash = hash;

    return NGX_OK;
}

static void print_sockaddr(ngx_log_t *log, struct sockaddr *addr) {
    struct sockaddr_in *ip;

    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, log, 0, "dynamic sockaddr: %s", "world");
    ip = (struct sockaddr_in *)addr;
    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, log, 0, "dynamic sockaddr: %s", inet_ntoa(ip->sin_addr));
}

static char *
ngx_http_upstream_dynamic_hash(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_upstream_srv_conf_t  *uscf;
    fprintf(stderr, "dynamic func %s\n", "hash");
    
    uscf = ngx_http_conf_get_module_srv_conf(cf, ngx_http_upstream_module);

    uscf->peer.init_upstream = ngx_http_upstream_init_dynamic_hash;

    uscf->flags = NGX_HTTP_UPSTREAM_CREATE
                  |NGX_HTTP_UPSTREAM_MAX_FAILS
                  |NGX_HTTP_UPSTREAM_FAIL_TIMEOUT
                  |NGX_HTTP_UPSTREAM_DOWN
		  |NGX_HTTP_UPSTREAM_WEIGHT;
    fprintf(stderr, "dynamic func %s\n", "hash");

    return NGX_CONF_OK;
}

static int h1(char* str, int len) {
    int b    = 378551;
    int a    = 63689;
    int hash = 0;
    int i    = 0;

    for(i = 0; i < len; str++, i++)
    {
        hash = hash * a + (*str);
        a    = a * b;
    }

    return hash>0?hash:-hash;
}

static int h2(char* str, int len) {
    int hash = 1315423911;
    int i    = 0;

    for(i = 0; i < len; str++, i++)
    {
        hash ^= ((hash << 5) + (*str) + (hash >> 2));
    }

    return hash>0?hash:-hash;
}

static void getPermutation(int** permutation, int row, int col, char** name) {
    int offset;
    int skip;
    int i,j;

    for (i=0; i<row; i++) {
        offset = h1(name[i], strlen(name[i])) % col;
        skip = h2(name[i], strlen(name[i])) % (col-1) + 1;
        for (j=0; j<col; j++) {
            permutation[i][j] = (offset + j*skip) % col;
        }
    }
}

static void init_peers(int row, int col, int* weight, char** name, int* entry) {

    int i;
    int* next;
    int* sum;
    int** permutation;
    int c;
    int n=0;

    next = (int*)malloc(sizeof(int) * row);
    sum = (int*)malloc(sizeof(int) * row);
    permutation = (int **)malloc(sizeof(int*) * row);
    for (i=0; i<row; i++) {
        permutation[i] = (int*)malloc(sizeof(int) * col);
    }

    for (i=0; i<row; i++) {
        next[i] = 0;
        sum[i] = 0;
    }

    for (i=0; i<col; i++) {
        entry[i] = -1;
    }


    getPermutation(permutation, row, col, name);

    while (1) {
        for (i=0; i<row; i++) {
            sum[i] += weight[i];
            while (sum[i] >= 1) {
                sum[i] -= 1;
                c = permutation[i][next[i]];
                while (entry[c] >= 0) {
                    next[i] = next[i]+1;
                    c = permutation[i][next[i]];
                }
                entry[c] = i;
                next[i] = next[i]+1;
                n = n+1;
                if (n == col) {
                    return;
                }
            }
        }
    }
}
