#include <linux/sysinfo.h>
#include <math.h>
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


struct sys_mem_stats {
  uint64_t total;
  uint64_t free;
  uint64_t used;
};

static int *_log_error_rank;

void get_memory_stats(struct sys_mem_stats *_sms);
int can_allocate(size_t _len, size_t _size);
void logging(int _rank, const char* _level, const char *_fmt, ...);
void critical_error_logger(int _signal);

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
    
    logging(__rank, "INFO", "Program started ------------------");

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

    logging(__rank, "INFO", "Parsing arguments");
    
    if (!argp_parse(&ap, argc, argv)) {
        logging(__rank, "ERROR", "Arguments parsing failed");
        argp_print_error(&ap);
        logging(__rank, "-----", "-----------------------------------\n");
        return 1;
    }

    const char *_input = argp_pos(&ap, "input");
    logging(__rank, "INFO", "Argument Input: %s", _input);
    
    int _test = argp_flag(&ap, "test");
    if (_test) logging(__rank, "INFO", "Test flag passed");

    const char *_output = argp_get(&ap, "output");
    logging(__rank, "INFO", "Argument Output: %s", _output);

    const char *_verbose = argp_get(&ap, "verbose");
    uint8_t verbosity_level = 2; // Default
    
    if (_verbose) {
        uint8_t arg = _verbose[0] - '0';
        if (arg < 0 || arg > 3) {
            print_error("Invalid argument",
                        "Use a number between 0 and 3... running with default level "
                        "of verbosity.",
                        NULL, -1);
            logging(__rank, "WARNING", "Invalid argument to option -v|--verbose");
            arg = 2;
        }
        verbosity_level = arg;
        logging(__rank, "INFO", "Verbosity level: %u", verbosity_level);
    }

    logging(__rank, "INFO", "Arguments parsed");
    // ----------------------------------------------------------------------------------------------------------------
    
    // ----------------------------------------------------------------------------------------------------------------
    //  C H E C K I N G   M E M O R Y   S T A T U S
    // ----------------------------------------------------------------------------------------------------------------
    
    // Each rank needs to verify your own memory and be shure of not use more than
    // 80% according to voices of my head

    logging(__rank, "INFO", "Starting first check of memory status");
    struct sys_mem_stats sms;
    get_memory_stats(&sms);
    
    test_op(sms.total, !=, -1, "Failed in check memory stats.");
    if (sms.total == -1) logging(__rank, "WARNING", "Cannot define memory status");
    else logging(__rank, "INFO", "Success getting memory status: TOTAL %luMB | FREE %luMB | USED %luMB", 
        sms.total / (1024 * 1024), sms.free / (1024 * 1024), sms.used / (1024 * 1024));
    
    char buff[256];
    snprintf(&(buff[0]), 256, "Total memory: %luMB | Free: %luMB | Used: %luMB",
             sms.total / (1024 * 1024), sms.free / (1024 * 1024),
             sms.used / (1024 * 1024));
    print_success(NULL, buff);
    
    // ----------------------------------------------------------------------------------------------------------------
    
    // ----------------------------------------------------------------------------------------------------------------
    //  R E A D I N G   I N P U T
    // ----------------------------------------------------------------------------------------------------------------

    logging(__rank, "INFO", "Start Reading file");
    uint32_t *V_TO_COMPONENTS;
    logging(__rank, "INFO", "Address of V_TO_COMPONENTS : %p", (void*)&V_TO_COMPONENTS);
    uint32_t vertex = 0, edges = 0;
    if( _test ){
        if(__rank == 0){
            if(strstr(_input, ".bin") != NULL){
                logging(__rank, "INFO", "Found .bin in input file");
                size_t len_suffix = strlen(".meta.txt");
                size_t len_i = strlen(_input);
                char *input = malloc((strlen(_input) - 4 + len_suffix + 1) * sizeof(char));
                strncpy(&(input[0]), _input, len_i - 4);
                input[len_i - 4] = '\0';
                strcat(input, ".meta.txt");
                logging(__rank, "INFO", "Opening file %s", input);
                FILE* fp = fopen(input, "r");
                int _ = fscanf(fp, "%u\n%u", &vertex, &edges);
                test(_ == 2);
                fclose(fp);
                logging(__rank, "TEST - INFO", "Readed information of vertex(%u) and edges(%u)", vertex, edges);
                
            }
        }
        if (__rank == 0) logging(__rank, "SEND - INFO", "Sending vertex and edges to other nodes");
        else logging(__rank, "RECV - INFO", "Receiving info from rank 0");
        MPI_Bcast(&vertex, 1, MPI_UINT32_T, 0, MPI_COMM_WORLD);
        MPI_Bcast(&edges, 1, MPI_UINT32_T, 0, MPI_COMM_WORLD);
    
        if (__rank == 0) logging(__rank, "SEND - INFO", "Sended");
        else logging(__rank, "RECV - INFO", "Received vertex(%u) and edges(%u)", vertex, edges);
    } else {
        vertex = 10000000, edges = 800000000;
        logging(__rank, "INFO", "Setting the giant dataset vertex(%u) and edges(%u)", vertex, edges);    
    }

    logging(__rank, "INFO", "Calculating the read offset");
    uint32_t base = edges / __size;
    uint32_t rem = edges % __size;
    uint32_t my_edges = base + (__rank < rem ? 1 : 0);
    uint32_t read_start = __rank * base + (__rank < rem ? __rank : rem);
    uint32_t read_end = read_start + my_edges - 1;
    logging(__rank, "INFO", "Offset calculated. START: %u | END: %u | N: %u", read_start, read_end, my_edges);

    MPI_File fh;
    MPI_Offset offset;
    logging(__rank, "INFO", "MPI opening file: %s", _input);
    MPI_File_open(MPI_COMM_WORLD, _input, MPI_MODE_RDONLY, MPI_INFO_NULL, &fh);
    logging(__rank, "INFO", "MPI file opened");
    
    logging(__rank, "INFO", "MPI Finding offset position");
    MPI_File_get_position(fh, &offset);
    MPI_File_seek(fh, offset * (read_start / sizeof(edge_t)), MPI_SEEK_SET);
    logging(__rank, "INFO", "MPI Setup offset complete");
    
    size_t max_items_to_allocate = -1;
    if((max_items_to_allocate = can_allocate(my_edges, sizeof(edge_t)))){
        logging(__rank, "INFO", "Can allocate itens: %u", max_items_to_allocate == 1 ? my_edges : max_items_to_allocate);
        edge_t *E = malloc((max_items_to_allocate == 1 ? my_edges : max_items_to_allocate) * sizeof(edge_t));
        test(E);
        logging(__rank, "INFO", "Memory to edges allocated successfully");

        logging(__rank, "INFO", "Free edges");
        free(E);
    } else {
        logging(__rank, "ERROR", "Not enough memory");
    }
    
    // ----------------------------------------------------------------------------------------------------------------
    


    // ----------------------------------------------------------------------------------------------------------------
    //  C L E A N   M E M O R Y
    // ----------------------------------------------------------------------------------------------------------------
    logging(__rank, "INFO", "Closing MPI File");
    MPI_File_close(&fh);
    logging(__rank, "INFO", "Stopping service");
    MPI_Finalize();
    logging(__rank, "INFO", "Finale muchacho");
    return 0;
}

void critical_error_logger(int _signal){
    switch ( _signal ){
        case SIGSEGV: {
            logging(*_log_error_rank, "CRITICAL ERROR", "CODE FAILURE...SEGMENTATION VIOLATION (CORE DUMP)\n\n");
        } break;
        case SIGFPE: {
            logging(*_log_error_rank, "CRITICAL ERROR", "CODE FAILURE...FLOATING-POINT EXCEPTION (CORE DUMP)\n\n");
        } break;
        case SIGILL: {
            logging(*_log_error_rank, "CRITICAL ERROR", "ILLEGAL INSTRUCTION (CORE DUMP)\n\n");
        } break;
        case SIGBUS: {
            logging(*_log_error_rank, "CRITICAL ERROR", "BUS ERROR (CORE DUMP)\n\n");
        } break;
        case SIGINT: {
            logging(*_log_error_rank, "CRITICAL ERROR", "TERMINAL INTERRUPT\n\n");
        } break;
        case SIGTERM: {
            logging(*_log_error_rank, "CRITICAL ERROR", "TERMINATION SIGNAL\n\n");
        } break;
        case SIGQUIT: {
            logging(*_log_error_rank, "CRITICAL ERROR", "TERMINAL QUIT (CORE DUMP)\n\n");
        } break;
        case SIGABRT: {
            logging(*_log_error_rank, "CRITICAL ERROR", "ABORT SIGNAL (CORE DUMP)\n\n");
        } break;
        case SIGHUP: {
            logging(*_log_error_rank, "CRITICAL ERROR", "HANGUP\n\n");
        } break;
        case SIGCHLD: {
            logging(*_log_error_rank, "CRITICAL ERROR", "CHILD STATUS CHANGED\n\n");
        } break;
        case SIGPIPE: {
            logging(*_log_error_rank, "CRITICAL ERROR", "BROKEN PIPE\n\n");
        } break;
    }
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

    struct sys_mem_stats sms;
    
    get_memory_stats(&sms);
    
    int32_t t = sms.total * 0.8;
    int32_t n = (t - sms.used) / _size;
    

    if ( _len * _size <= n ){
        return 1; // Okay, you can allocate that
    }
    else if (n > 0){
        return n; // Damn, you so unluck, but you can allocate that
    }
    
    return 0; // OMG, do you colou chiclete na cruz nigga? You cant allocate nothing
}

void logging(int _rank, const char* _level, const char *_fmt, ...){
    test_op(_rank, >=, 0);
    struct timeval time;
    gettimeofday(&time, NULL);
    
    char filename[256];
    snprintf(filename, sizeof(filename), "./logs/rank%d.txt", _rank);

    FILE *fp = fopen(filename, "a+");
    test(fp);
    
    char prefix[256];
    snprintf(prefix, sizeof(prefix), "[%llu] RANK %d | [%s] --- ",
            (unsigned long long)time.tv_sec * 1000000 + time.tv_usec, _rank, _level);
    
    char content[2048];
    va_list args;
    va_start(args, _fmt);
    vsnprintf(content, sizeof(content), _fmt, args);
    va_end(args);
    
    fprintf(fp, "%s%s\n", prefix, content);
    
    fclose(fp);
   
}



































// MPI_File fh;
// MPI_Offset file_size;

// char input[256];
// snprintf(&(input[0]), 255, "%s_%d", _input, __rank);
// logging(__rank, "INFO", "Input file name: %s", input);

// MPI_File_open(MPI_COMM_WORLD, _input, MPI_MODE_RDONLY, MPI_INFO_NULL, &fh);
// logging(__rank, "INFO", "MPI Opened File successfully");
// MPI_File_get_size(fh, &file_size);
// logging(__rank, "INFO", "MPI get file size: %llu", file_size);

// // file_buffer = malloc(file_size + 1);
// // test(file_buffer, "RANK %d: Failed to allocate file buffer", __rank);

// // MPI_File_read_at_all(fh, 0, file_buffer, file_size, MPI_BYTE, MPI_STATUS_IGNORE);
// // MPI_File_close(&fh);
// // file_buffer[file_size] = '\0';

// MPI_Offset file_offset;
// if (__rank == 0) {
//     char file_buffer[17];
//     MPI_File_read(fh, file_buffer, 16, MPI_CHAR, MPI_STATUS_IGNORE);
//     file_buffer[15] = '\0';
//     int parsed = sscanf(file_buffer, "%u\n%u", &vertex, &edges);
//     test_op(parsed, ==, 2, "Failed to parse vertices/edges count");
//     test_op(vertex, >, 0, "Invalid vertex count");
//     test_op(edges, >, 0, "Invalid edge count");
//     logging(__rank, "INFO", "Vertex: %u | Edges: %u", vertex, edges);
//     MPI_File_get_position(fh, &file_offset);
// }

// MPI_Bcast(&vertex, 1, MPI_UINT32_T, 0, MPI_COMM_WORLD);
// MPI_Bcast(&edges, 1, MPI_UINT32_T, 0, MPI_COMM_WORLD);
// MPI_Bcast(&file_offset, 1, MPI_OFFSET, 0, MPI_COMM_WORLD);

// uint32_t base = edges / __size;
// uint32_t rem = edges % __size;
// uint32_t my_edges = base + (__rank < rem ? 1 : 0);
// uint32_t read_start = __rank * base + (__rank < rem ? __rank : rem);
// uint32_t read_end = read_start + my_edges - 1;

// logging(__rank, "INFO", "Reading from %u to %u", read_start, read_end);
// test_op(read_end, >=, read_start, "RANK %d: No edges assigned", __rank);

// if (verbosity_level >= 3) {
//     char buff[256];
//     snprintf(buff, sizeof(buff),
//              "RANK %d:\n\tReading edges %u to %u (%u edges)",
//              __rank, read_start, read_end, my_edges);
//     print_success(NULL, buff);
// }

// uint32_t newline_count = 0;
// MPI_Offset edge_start_pos = 0;
// for (MPI_Offset i = 0; i < file_size; i++) {
//     if (file_buffer[i] == '\n') {
//         newline_count++;
//         if (newline_count == 2) {
//             edge_start_pos = i + 1;
//             break;
//         }
//     }
// }

// test(can_allocate(my_edges, sizeof(edge_t)), "RANK %d: Doesnt have sufficient memory", __rank);
// edge_t *local_edges = malloc(my_edges * sizeof(edge_t));
// test(local_edges, "RANK %d: Failed to allocate local edges", __rank);

// {
//     const char *line = file_buffer + edge_start_pos;

//     for (uint32_t i = 0; i < read_start; ++i) {
//         const char *nl = line;
//         while (*nl != '\n' && *nl != '\0') nl++;
//         line = *nl == '\n' ? nl + 1 : nl;
//     }

//     for (uint32_t i = 0; i < my_edges; ++i) {
//         const char *nl = line;
//         while (*nl != '\n' && *nl != '\0') nl++;

//         sscanf(line, "%d %d %ld",
//                &local_edges[i].src,
//                &local_edges[i].dest,
//                &local_edges[i].weight);

//         line = *nl == '\n' ? nl + 1 : nl;
//     }
// }

// V_TO_COMPONENTS[__rank] = malloc(vertex * sizeof(uint32_t));
// test(V_TO_COMPONENTS[__rank], "RANK %d: Failed to allocate V_TO_COMPONENTS", __rank);
// setup_index_components_vector(&(V_TO_COMPONENTS[__rank]), vertex);
// test_op(V_TO_COMPONENTS[__rank][0], ==, 0,
//         "RANK %d: Setup of V_TO_COMPONENTS failed.", __rank);

// free(local_edges);
// free(file_buffer);

// if ( __rank == 0){
//     FILE *input = fopen(_input, "r");
//     test(input, "It was not be possible open the file...");
    
//     test( fscanf(input, "%d\n%d", &vertex, &edges) );
//     test_op( vertex > 0, &&, edges > 0, "RANK %d: Failed to read file", __rank);

//     for(size_t i = 1; i < __size; ++i){
//         MPI_Send(&vertex, 1, MPI_UINT32_T, i, 0, MPI_COMM_WORLD);
//         MPI_Send(&edges, 1, MPI_UINT32_T, i, 0, MPI_COMM_WORLD);
//     }

//     uint32_t read_start, read_end;
    
//     read_start =  (edges / __size) * __rank;
//     read_end = ((edges / __size) * ( __rank + 1 )) - 1;

//     test_op(read_end, >, read_start, "Error calculating the offset to read the file");

//     print_success(NULL, "Read Start: %u | Read End: %u", read_start, read_end);
// }
// else {
//     MPI_Recv(&vertex, 1, MPI_UINT32_T, 0, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
//     MPI_Recv(&edges, 1, MPI_UINT32_T, 0, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

//     test_op((vertex != 0), &&, (edges != 0), "RANK %d: Error in communication...", __rank);

//     uint32_t read_start, read_end;
    
//     read_start =  (edges / __size) * __rank;
//     read_end = ((edges / __size) * ( __rank + 1 )) - 1;

//     test_op(read_end, >, read_start, "Error calculating the offset to read the file");

//     FILE *input = fopen(_input, "r");

//     test(input);

//     fseek
    
// }

// for(size_t i = 0; i < __size; ++i){
//     test( can_allocate(vertex, sizeof(int32_t)) );

//     // Data division
//     uint32_t read_start, read_end;

//     if ( __rank == i ){
//         read_start =  (edges / __size) * __rank;
//         read_end = ((edges / __size) * ( __rank + 1 )) - 1;

//         test_op(read_start, !=, read_end);

//         char buff[256];
//         snprintf(&(buff[0]), sizeof(buff) - 1, "RANK %d:\n\tReading from: %lu\n\tto: %lu", __rank, 
//             (unsigned long)read_start, (unsigned long)read_end);
//         print_success(NULL, buff);

//         // TODO: Comunicate who cannot (NECESSARY?)

//         setup_index_components_vector(&(V_TO_COMPONENTS[i]), vertex);
//         test_op(V_TO_COMPONENTS[i][0], == , 0, "Setup of V_TO_COMPONENTS failed.");
//     }

    
//     free(V_TO_COMPONENTS[i]);   
// }

// }