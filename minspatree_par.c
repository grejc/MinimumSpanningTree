#include <linux/sysinfo.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/sysinfo.h>
#include <sys/time.h>
#include <unistd.h>
#include <stdarg.h>
#include <signal.h>

#include "mpi.h"
#ifdef GREJC_SETUP_MPI_DEFAULT
#include <mpi/mpi.h>
#elif GREJC_SETUP_MPI_FALLBACK
#include <mpi.h>
#endif

#include "utils.h"
#include "graph.h"

#include <stdlib.h>

struct sys_mem_stats {
  uint64_t total;
  uint64_t free;
  uint64_t used;
};

typedef enum {
    INFO,
    SEND,
    RECV,
    WARNING,
    ERROR,
    CRITICAL_ERROR
} log_level_t;

static int *_log_error_rank;
static struct timeval _t0;

void __startup__(void) __attribute__((constructor));

void __startup__(){
    gettimeofday(&_t0, NULL);
}

void get_memory_stats(struct sys_mem_stats *_sms);
int can_allocate(size_t _len, size_t _size);
void logging(int _rank, log_level_t _level, const char *_fmt, ...);
void critical_error_logger(int _signal);
int preferable_over(edge_t u, edge_t v);

void reduce_best_edge(void* invec, void* inoutvec, int* len, MPI_Datatype* datatype) {
    edge_t* in = (edge_t*)invec;
    edge_t* inout = (edge_t*)inoutvec;

    for (int i = 0; i < *len; i++) {
        if (inout[i].src == 10000001) {
            inout[i] = in[i];
            continue;
        }
        if (in[i].src == 10000001) {
            continue;
        }
        if (in[i].w < inout[i].w) {
            inout[i] = in[i];
        }
        else if (in[i].w == inout[i].w) {
            uint32_t in_ns = in[i].src < in[i].dst ? in[i].src : in[i].dst;
            uint32_t in_nd = in[i].src < in[i].dst ? in[i].dst : in[i].src;
            uint32_t out_ns = inout[i].src < inout[i].dst ? inout[i].src : inout[i].dst;
            uint32_t out_nd = inout[i].src < inout[i].dst ? inout[i].dst : inout[i].src;
            if (in_ns < out_ns || (in_ns == out_ns && in_nd < out_nd)) {
                inout[i] = in[i];
            }
        }
    }
}

int main(int argc, char **argv) {  
    MPI_Init(&argc, &argv);
    int __rank, __size;

    _log_error_rank = &__rank;

    signal(SIGSEGV, (void (*)(int))critical_error_logger);
    signal(SIGFPE, (void (*)(int))critical_error_logger);
    signal(SIGILL, (void (*)(int))critical_error_logger);
    signal(SIGBUS, (void (*)(int))critical_error_logger);
    signal(SIGINT, (void (*)(int))critical_error_logger);
    signal(SIGTERM, (void (*)(int))critical_error_logger);
    signal(SIGQUIT, (void (*)(int))critical_error_logger);
    signal(SIGABRT, (void (*)(int))critical_error_logger);
    signal(SIGHUP, (void (*)(int))critical_error_logger);
    signal(SIGCHLD, (void (*)(int))critical_error_logger);
    signal(SIGPIPE, (void (*)(int))critical_error_logger);
    
    MPI_Comm_rank(MPI_COMM_WORLD, &__rank);
    MPI_Comm_size(MPI_COMM_WORLD, &__size);
    
    logging(__rank, INFO, "Program started -----------------------------------------------");

    // ----------------------------------------------------------------------------------------------------------------
    // P A R S I N G   A R G U M E N T S
    // ----------------------------------------------------------------------------------------------------------------
    
    ArgParser ap;
    argp_init(&ap, "minspatree_par.out", "1.0.0",
            "Parallel Minimum Spanning Tree.");
    argp_add_flag(
        &ap, 't', "test",
        "Use the input format from sequential code (prevents your PC 💥).");
    argp_add_option(&ap, 'v', "verbose", "VALUE", "Set the verbosity level:\n\t\
                        0 → No messages (the result goes to -o|--output)\n\t\
                        1 → See results and basic error messages in stdout\n\t\
                        2 → 1 + Style\n\t\
                        3 → 2 + Debug detailed\n",
                    "2");
    argp_add_option(&ap, 'o', "output", "FILE", "Output file for the MST",
                    "./out_par.bin");
    argp_add_pos(&ap, "input", "Input file with dataset", 1);

    logging(__rank, INFO, "Parsing arguments");
    
    if (!argp_parse(&ap, argc, argv)) {
        logging(__rank, ERROR, "Arguments parsing failed");
        argp_print_error(&ap);
        logging(__rank, INFO, "-----------------------------------\n");
        return 1;
    }

    const char *_input = argp_pos(&ap, "input");
    logging(__rank, INFO, "Argument Input: %s", _input);
    
    int _test = argp_flag(&ap, "test");
    if (_test) logging(__rank, INFO, "Test flag passed");

    const char *_output = argp_get(&ap, "output");
    logging(__rank, INFO, "Argument Output: %s", _output);

    const char *_verbose = argp_get(&ap, "verbose");
    uint8_t verbosity_level = 2; // Default
    
    if (_verbose) {
        uint8_t arg = _verbose[0] - '0';
        if (arg < 0 || arg > 3) {
            print_error("Invalid argument",
                        "Use a number between 0 and 3... running with default level "
                        "of verbosity.",
                        NULL, -1);
            logging(__rank, WARNING, "Invalid argument to option -v|--verbose");
            arg = 2;
        }
        verbosity_level = arg;
        logging(__rank, INFO, "Verbosity level: %u", verbosity_level);
    }

    logging(__rank, INFO, "Arguments parsed");

    logging(__rank, INFO, "Creating type MPI_EDGE_T");
    MPI_Datatype MPI_EDGE_T;
    int MPI_EDGE_T_len[] = {1, 1, 1};
    MPI_Datatype MPI_EDGE_T_types[] = {MPI_UINT32_T, MPI_UINT32_T, MPI_DOUBLE};
    MPI_Aint MPI_EDGE_T_offsets[3];
    MPI_EDGE_T_offsets[0] = offsetof(edge_t, src);
    MPI_EDGE_T_offsets[1] = offsetof(edge_t, dst);
    MPI_EDGE_T_offsets[2] = offsetof(edge_t, w);
    
    MPI_Type_create_struct(3, MPI_EDGE_T_len, MPI_EDGE_T_offsets, MPI_EDGE_T_types, &MPI_EDGE_T);
    MPI_Type_commit(&MPI_EDGE_T);

    logging(__rank, INFO, "Creating type MPI_EDGE_T completed");

    logging(__rank, INFO, "Creating operation MPI_EDGE_W_COMPARE");
    // MPI_Op 
    logging(__rank, INFO, "Creating operation MPI_BEST_EDGE_OP completed");
    MPI_Op MPI_BEST_EDGE_OP;
    MPI_Op_create(reduce_best_edge, 1, &MPI_BEST_EDGE_OP);
    
    // ----------------------------------------------------------------------------------------------------------------
    
    // ----------------------------------------------------------------------------------------------------------------
    //  C H E C K I N G   M E M O R Y   S T A T U S
    // ----------------------------------------------------------------------------------------------------------------
    
    // Each rank needs to verify your own memory and be shure of not use more than
    // 80% according to voices of my head

    logging(__rank, INFO, "Starting first check of memory status");
    struct sys_mem_stats sms;
    get_memory_stats(&sms);
    
    // test_op(sms.total, !=, -1, "Failed in check memory stats.");
    if (sms.total == -1) logging(__rank, WARNING, "Cannot define memory status");
    else logging(__rank, INFO, "Success getting memory status: TOTAL %luMB | FREE %luMB | USED %luMB", 
        sms.total / (1024 * 1024), sms.free / (1024 * 1024), sms.used / (1024 * 1024));
    
    // char buff[256];
    // snprintf(&(buff[0]), 256, "RANK %d Total memory: %luMB | Free: %luMB | Used: %luMB",
    //          __rank, sms.total / (1024 * 1024), sms.free / (1024 * 1024),
    //          sms.used / (1024 * 1024));
    // print_success(NULL, buff);
    
    // ----------------------------------------------------------------------------------------------------------------
    
    // ----------------------------------------------------------------------------------------------------------------
    //  R E A D I N G   I N P U T
    // ----------------------------------------------------------------------------------------------------------------

    // This array will store which component belongs to the vertex (INDEX_V2C[edge_id] → component_id)
    uint32_t *INDEX_V2C = NULL;
    // This array will store the best edge of an component (BEST_EDGE2C[component_id] → edge)
    edge_t *BEST_EDGE2C = NULL;
    // Global array to reduce the best edges
    edge_t *GLOBAL_BEST_EDGES = NULL;

    logging(__rank, INFO, "Start Reading file");
    logging(__rank, INFO, "Address of INDEX_V2C : %p", (void*)&INDEX_V2C);
    uint32_t vertex = 0, edges = 0;
    if( _test ){
        if(__rank == 0){
            if(strstr(_input, ".bin") != NULL){
                logging(__rank, INFO, "Found .bin in input file");
                size_t len_suffix = strlen(".meta.txt");
                size_t len_i = strlen(_input);
                char *input = malloc((strlen(_input) - 4 + len_suffix + 1) * sizeof(char));
                strncpy(&(input[0]), _input, len_i - 4);
                input[len_i - 4] = '\0';
                strcat(input, ".meta.txt");
                logging(__rank, INFO, "Opening file %s", input);
                FILE* fp = fopen(input, "r");
                int _ = fscanf(fp, "%u\n%u", &vertex, &edges);
                test(_ == 2);
                fclose(fp);
                logging(__rank, INFO, "TEST - Readed information of vertex(%u) and edges(%u)", vertex, edges);
                
            }
        }
        if (__rank == 0) logging(__rank, SEND, "Sending vertex and edges to other nodes");
        else logging(__rank, RECV, "Receiving info from rank 0");
        MPI_Bcast(&vertex, 1, MPI_UINT32_T, 0, MPI_COMM_WORLD);
        MPI_Bcast(&edges, 1, MPI_UINT32_T, 0, MPI_COMM_WORLD);
    
        if (__rank == 0) logging(__rank, SEND, "Sended");
        else logging(__rank, RECV, "Received vertex(%u) and edges(%u)", vertex, edges);
    } else {
        // TODO: Implement dynamic calculation based on mpi get file size
        vertex = 10000000, edges = 800000000;
        logging(__rank, INFO, "Setting the giant dataset vertex(%u) and edges(%u)", vertex, edges);    
    }

    logging(__rank, INFO, "Calculating the read offset");
    uint32_t base = edges / __size;
    uint32_t rem = edges % __size;
    uint32_t my_edges = base + (__rank < rem ? 1 : 0);
    uint32_t _start = (__rank * base + (__rank < rem ? __rank : rem));
    uint32_t _end = _start + my_edges - 1;
    logging(__rank, INFO, "Offset calculated. START: %u | END: %u | N: %u", _start, _end, my_edges);
    MPI_Offset start_r = sizeof(edge_t) * _start;
    MPI_Offset end_r = sizeof(edge_t) * _end;

    MPI_File fh;
    logging(__rank, INFO, "MPI opening file: %s", _input);
    MPI_File_open(MPI_COMM_WORLD, _input, MPI_MODE_RDONLY, MPI_INFO_NULL, &fh);
    logging(__rank, INFO, "MPI file opened");
    
    // logging(__rank, INFO, "MPI Finding offset position");
    // MPI_File_seek(fh, read_start, MPI_SEEK_SET);
    // logging(__rank, INFO, "MPI Setup offset complete");
    
    // size_t max_items_to_allocate = -1;
    // if((max_items_to_allocate = can_allocate(my_edges, sizeof(edge_t))) > 0){
    if (can_allocate(my_edges, sizeof(edge_t)) == my_edges
        && can_allocate(vertex, sizeof(uint32_t)) == vertex
        && can_allocate(vertex, sizeof(edge_t)) == vertex
    ){
        logging(__rank, INFO, "Can allocate edges(%u)", my_edges);
        edge_t *E = malloc((my_edges) * sizeof(edge_t));
        test(E);
        MPI_Status read_status;
        logging(__rank, INFO, "Memory to edges allocated successfully on %p", (void*)E);
        logging(__rank, INFO, "Setting edges from file to memory");
        MPI_File_read_at_all(fh, start_r, E, my_edges, MPI_EDGE_T, &read_status);
        int count;
        MPI_Get_count(&read_status, MPI_EDGE_T, &count);
        logging(__rank, INFO, "Setting edges from file to memory completed with count %d", count);

        logging(__rank, INFO, "First edge: u(%u) v(%u) w(%.9f)", E[0].src, E[0].dst, E[0].w);

        logging(__rank, INFO, "Setup of components array");
        if (can_allocate(vertex, sizeof(uint32_t)) == vertex){
            INDEX_V2C = malloc(vertex * sizeof(uint32_t));
            test(INDEX_V2C);
            logging(__rank, INFO, "INDEX_V2C allocated... Init indexes...");
            setup_index_components_vector(&INDEX_V2C, vertex);
            logging(__rank, INFO, "Init indexes completed | INDEX_V2C[0] = %u | INDEX_V2C[%u] = %u", INDEX_V2C[0], vertex - 1, INDEX_V2C[vertex - 1]);
            size_t components_len = vertex;
            BEST_EDGE2C = malloc(sizeof(edge_t) * components_len);
            test(BEST_EDGE2C);
            test_op(can_allocate(components_len, sizeof(edge_t)), ==, components_len);
            GLOBAL_BEST_EDGES = malloc(sizeof(edge_t) * components_len);
            test(GLOBAL_BEST_EDGES);
        
            size_t best_edges_len = 0;

            // --------------------------------------------------------------------------------------------------------
            // S T A R T   P R O C E S S I N G   L O O P 
            // --------------------------------------------------------------------------------------------------------

            logging(__rank, INFO, "Start processing data...");
            int done = 0;
            while ( !done ){

                logging(__rank, INFO, "Reseting best edges...");
                // RESET
                for(size_t i = 0; i < components_len; ++i){
                    BEST_EDGE2C[i].src = BEST_EDGE2C[i].dst = 10000001;
                    BEST_EDGE2C[i].w = -1; // irrelevant
                }
                logging(__rank, INFO, "Reseting best edges... completed.");

                logging(__rank, INFO, "Executing local search...");
                // LOCAL SEARCH
                for (size_t i = 0; i < my_edges; ++i){
                    uint32_t component_edge_u = INDEX_V2C[E[i].src];
                    uint32_t component_edge_v = INDEX_V2C[E[i].dst];

                    if (component_edge_u != component_edge_v){
                        if(preferable_over(E[i], BEST_EDGE2C[component_edge_u])){
                            BEST_EDGE2C[component_edge_u] = E[i];
                        }
                        
                        if(preferable_over(E[i], BEST_EDGE2C[component_edge_v])){
                            BEST_EDGE2C[component_edge_v] = E[i];
                        }
                    }
                }
                logging(__rank, INFO, "Executing local search... completed");

                logging(__rank, INFO, "Sync with MPI_Allreduce...");
                // SYNC
                MPI_Allreduce(BEST_EDGE2C, GLOBAL_BEST_EDGES, components_len, MPI_EDGE_T, MPI_BEST_EDGE_OP, MPI_COMM_WORLD);
                logging(__rank, INFO, "Sync with MPI_Allreduce... completed");


                logging(__rank, INFO, "Join components...");
                // JOIN COMPONENTS
                for(size_t i = 0; i < components_len; ++i){
                    if ( GLOBAL_BEST_EDGES[i].src == 10000001 ) continue;

                    // TODO: Add the edge to the final list of MST
                    best_edges_len++;

                    uint32_t u_root = INDEX_V2C[GLOBAL_BEST_EDGES[i].src];
                    uint32_t v_root = INDEX_V2C[GLOBAL_BEST_EDGES[i].dst];
                    if ( u_root < v_root ) {
                        INDEX_V2C[GLOBAL_BEST_EDGES[i].dst] = u_root;
                    } else {
                        INDEX_V2C[GLOBAL_BEST_EDGES[i].src] = v_root;
                    }
                }
                logging(__rank, INFO, "Join components... completed");

                
                logging(__rank, INFO, "Compressing paths...");
                // COMPRESS PATHS
                for(size_t i = 0; i < vertex; ++i) {
                    uint32_t root = INDEX_V2C[i];
                    while ( root != INDEX_V2C[root] )
                        root = INDEX_V2C[root];
                    INDEX_V2C[i] = root;
                }
                logging(__rank, INFO, "Compressing paths... completed");

                logging(__rank, INFO, "Check convergence...");
                // CHECK CONVERGENCE
                done = 1;
                uint32_t first_root = INDEX_V2C[0];
                for(size_t i = 1; i < vertex; ++i) {
                    if (INDEX_V2C[i] != first_root) {
                        done = 0;
                        break;
                    }
                }
                logging(__rank, INFO, "Check convergence... completed");
                
            }            
        } else {
            // TODO: Make a fallback
            logging(__rank, ERROR, "Setup components failed. Not enough memory...");
        }
        
        logging(__rank, INFO, "Free edges");
        free(E);
        free(INDEX_V2C);
        logging(__rank, INFO, "Free edges completed");
    } else {
        //TODO: Make blocks reading
        logging(__rank, WARNING, "Not enough memory... starting fallback reading in blocks...");
    }
    
    // ----------------------------------------------------------------------------------------------------------------
    


    // ----------------------------------------------------------------------------------------------------------------
    //  C L E A N   M E M O R Y
    // ----------------------------------------------------------------------------------------------------------------
    MPI_Op_free(&MPI_BEST_EDGE_OP);
    MPI_Type_free(&MPI_EDGE_T);
    logging(__rank, INFO, "Closing MPI File");
    MPI_File_close(&fh);
    logging(__rank, INFO, "Stopping service");
    MPI_Finalize();
    logging(__rank, INFO, "Finale muchacho\n");
    return 0;
}

void critical_error_logger(int _signal){
    switch ( _signal ){
        case SIGSEGV: {
            logging(*_log_error_rank, CRITICAL_ERROR, "CODE FAILURE...SEGMENTATION VIOLATION (CORE DUMP)\n\n");
        } break;
        case SIGFPE: {
            logging(*_log_error_rank, CRITICAL_ERROR, "CODE FAILURE...FLOATING-POINT EXCEPTION (CORE DUMP)\n\n");
        } break;
        case SIGILL: {
            logging(*_log_error_rank, CRITICAL_ERROR, "ILLEGAL INSTRUCTION (CORE DUMP)\n\n");
        } break;
        case SIGBUS: {
            logging(*_log_error_rank, CRITICAL_ERROR, "BUS ERROR (CORE DUMP)\n\n");
        } break;
        case SIGINT: {
            logging(*_log_error_rank, CRITICAL_ERROR, "TERMINAL INTERRUPT\n\n");
        } break;
        case SIGTERM: {
            logging(*_log_error_rank, CRITICAL_ERROR, "TERMINATION SIGNAL\n\n");
        } break;
        case SIGQUIT: {
            logging(*_log_error_rank, CRITICAL_ERROR, "TERMINAL QUIT (CORE DUMP)\n\n");
        } break;
        case SIGABRT: {
            logging(*_log_error_rank, CRITICAL_ERROR, "ABORT SIGNAL (CORE DUMP)\n\n");
        } break;
        case SIGHUP: {
            logging(*_log_error_rank, CRITICAL_ERROR, "HANGUP\n\n");
        } break;
        case SIGCHLD: {
            logging(*_log_error_rank, CRITICAL_ERROR, "CHILD STATUS CHANGED\n\n");
        } break;
        case SIGPIPE: {
            logging(*_log_error_rank, CRITICAL_ERROR, "BROKEN PIPE\n\n");
        } break;
    }
    logging(*_log_error_rank, CRITICAL_ERROR, "exit(1)\n\n");
    exit(1);
}

void get_memory_stats(struct sys_mem_stats *_sms) {
    if (!_sms)
        return;
    
    struct sysinfo si;
    
    if (sysinfo(&si) == 0) {
        _sms->total = (uint64_t)si.totalram * si.mem_unit;
        _sms->free = (uint64_t)si.freeram * si.mem_unit;
        _sms->used = _sms->total - _sms->free;
        
        // Avoid explode my PC. 4 because I running mpirun -n 4
        #ifdef GREJC_SETUP_MPI_DEFAULT
            _sms->free /= 4;
        #endif
        
        return;
    }
    
    // Fallback: Parse /proc/meminfo
    FILE *fp = fopen("/proc/meminfo", "r");
    if (fp) {
        char line[256];
        int found = 0;
        while (fgets(line, sizeof(line), fp)) {
            if (strncmp(line, "MemTotal:", 9) == 0) {
                sscanf(line + 9, "%lu", &_sms->total);
                _sms->total *= 1024; // convert kB to bytes
                found++;
            } else if (strncmp(line, "MemFree:", 8) == 0) {
                sscanf(line + 8, "%lu", &_sms->free);
                _sms->free *= 1024; // convert kB to bytes
                found++;
            }
            if (found == 2)
            break;
        }
        fclose(fp);
        _sms->used = _sms->total - _sms->free;
    
        #ifdef GREJC_SETUP_MPI_DEFAULT
            _sms->free /= 4;
        #endif
        return;
    }
    
    _sms->free = _sms->used = _sms->total = 0;
}

int can_allocate(size_t _len, size_t _size) {
    if (_len <= 0 || _size <= 0) return -1; // How this is possible? Its literelly UNSIGNED, just don't be stupid to pass 0, but... 
                                            // If a programmer creates an idiot-proof system, the universe creates an idiot who will crash the system
    struct sys_mem_stats sms;
    
    get_memory_stats(&sms);
    
    int32_t t = sms.total * 0.8;
    int32_t n = (t - sms.used) / _size;
    size_t cost = _len * _size;

    if ( cost <= n ){
        return _len;
    } else if (n > 0) {
        return n;
    }
    
    return 0; // OMG, do you colou chiclete na cruz nigga? You cant allocate nothing
}

void logging(int _rank, log_level_t _level, const char *_fmt, ...){
    test_op(_rank, >=, 0);
    struct timeval time;
    gettimeofday(&time, NULL);
    time.tv_sec = time.tv_sec - _t0.tv_sec;
    time.tv_usec = time.tv_usec - _t0.tv_usec;
    if(time.tv_usec < 0) time.tv_usec += 1000000, time.tv_sec--;
    
    char filename[256];
    snprintf(filename, sizeof(filename), "./logs/rank%d.txt", _rank);

    FILE *fp = fopen(filename, "a+");
    test(fp);

    char lvl[16];
    switch (_level) {
        case INFO: {
            strncpy(lvl, "INFO", sizeof(lvl));
            lvl[5] = '\0';
        } break;
        case SEND: {
            strncpy(lvl, "SEND", sizeof(lvl));
            lvl[5] = '\0';
        } break;
        case RECV: {
            strncpy(lvl, "RECV", sizeof(lvl));
            lvl[5] = '\0';
        } break;
        case WARNING: {
            strncpy(lvl, "WARNING", sizeof(lvl));
            lvl[8] = '\0';
        } break;
        case ERROR: {
            strncpy(lvl, "ERROR", sizeof(lvl));
            lvl[6] = '\0';
        } break;
        case CRITICAL_ERROR: {
            strncpy(lvl, "CRITICAL ERROR", sizeof(lvl));
            lvl[15] = '\0';
        } break; 
    }
    
    char prefix[256];
    snprintf(prefix, sizeof(prefix), "[%ld.%ld] RANK %d | [%s] --- ",
            time.tv_sec, time.tv_usec, _rank, lvl);
    
    char content[2048];
    va_list args;
    va_start(args, _fmt);
    vsnprintf(content, sizeof(content), _fmt, args);
    va_end(args);
    
    fprintf(fp, "%s%s\n", prefix, content);
    
    fclose(fp);
}

int preferable_over(edge_t u, edge_t v){
    if ( u.src == 10000001 ) {
        return 1;
    }
    if (u.w < v.w) {
        return 1;
    }
    if(u.w == v.w){
        int ns = u.src < u.dst ? u.src : u.dst;
        int nd = u.src < u.dst ? u.dst : u.src;
        int cs = v.src < v.dst ? v.src : v.dst;
        int cd = v.src < v.dst ? v.dst : v.src;
        return ns < cs || (ns == cs && nd < cd);
    }
    return 0;
}
