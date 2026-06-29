// ============================================================================
// PARALELIZAÇÃO BORUVKA - PIPELINE DISCO/REDE/CPU
// Restrições: Arquivo apenas no Rank 0, Memória < 80%, Logs em todos os Ranks
// ============================================================================

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <float.h>
#include <mpi.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/sysinfo.h>
#include <sys/time.h>

#include "utils.h"

#define INF32 0xFFFFFFFF
#define _10M 10000000
#define _800M 800000000
#define root_code if ( RANK == 0 )
#define worker_code if ( RANK != 0 )
#define MIN(a, b) ((a) < (b) ? (a) : (b))

//=============================================================================
//===                              ESTRUTURAS                               ===
//=============================================================================

typedef struct edge_s {
    uint32_t src;
    uint32_t dst;
    double w;
} edge_t;

typedef struct component_s {
    uint32_t _id;
    edge_t best_edge;
} component_t;

typedef struct sparse_msg_s {
    uint32_t comp_id;
    uint32_t src;
    uint32_t dst;
    double w;
} sparse_msg_t;

struct sys_mem_stats {
    uint64_t total;
    uint64_t free;
    uint64_t used;
};

typedef enum { INFO, WARNING, ERROR, CRITICAL_ERROR } log_level_t;

//=============================================================================
//===                                 GLOBAIS                               ===
//=============================================================================

static int *_log_error_rank;
static struct timeval _t0;

//=============================================================================
//===                          DEFINIÇÃO DE FUNÇÕES                         ===
//=============================================================================

uint32_t find_component( uint32_t vertex_id, component_t *components );
void get_memory_stats( struct sys_mem_stats *_sms );
static int compare_edges( const void *a, const void *b );
bool preferable_over( const edge_t *new_edge, const edge_t *current );
void critical_error_logger( int _signal );
void logging( int _rank, log_level_t _level, const char *_fmt, ... );

void __startup__( void ) __attribute__( ( constructor ) );
void __startup__() { gettimeofday( &_t0, NULL ); }

//=============================================================================
//===                               MAIN                                    ===
//=============================================================================

int main( int argc, char **argv ) {
    bool _debug = true;
    char input_path[256] = "/home/null/college/ProgParalela/Test/graph.bin";

    if ( argc > 1 ) {
        _debug = true;
        strcpy( input_path, "/home/local/rgm42426/graph.bin");
    }

    MPI_Init( &argc, &argv );
    int RANK, SIZE;
    MPI_Comm_rank( MPI_COMM_WORLD, &RANK );
    MPI_Comm_size( MPI_COMM_WORLD, &SIZE );
    _log_error_rank = &RANK;

    logging( RANK, INFO, "LINE %d | Input Path: %s", __LINE__, input_path );

    //=========================================================================
    //===                   CRIAR TIPOS MPI                                  ===
    //=========================================================================

    MPI_Datatype MPI_EDGE_T, MPI_SPARSE_MSG_T;

    {
        int blocklens[] = { 1, 1, 1 };
        MPI_Aint offsets[] = { offsetof( edge_t, src ), offsetof( edge_t, dst ), offsetof( edge_t, w ) };
        MPI_Datatype types[] = { MPI_UINT32_T, MPI_UINT32_T, MPI_DOUBLE };
        MPI_Type_create_struct( 3, blocklens, offsets, types, &MPI_EDGE_T );
        MPI_Type_commit( &MPI_EDGE_T );
    }

    {
        int blocklens[] = { 1, 1, 1, 1 };
        MPI_Aint offsets[] = {
            offsetof( sparse_msg_t, comp_id ),
            offsetof( sparse_msg_t, src ),
            offsetof( sparse_msg_t, dst ),
            offsetof( sparse_msg_t, w )
        };
        MPI_Datatype types[] = { MPI_UINT32_T, MPI_UINT32_T, MPI_UINT32_T, MPI_DOUBLE };
        MPI_Type_create_struct( 4, blocklens, offsets, types, &MPI_SPARSE_MSG_T );
        MPI_Type_commit( &MPI_SPARSE_MSG_T );
    }

    logging( RANK, INFO, "LINE %d | MPI Types Created", __LINE__ );

    //=========================================================================
    //===                   INICIALIZANDO LOGGERS                            ===
    //=========================================================================

    signal( SIGSEGV, ( void ( * )( int ) )critical_error_logger );
    signal( SIGFPE, ( void ( * )( int ) )critical_error_logger );
    signal( SIGILL, ( void ( * )( int ) )critical_error_logger );
    signal( SIGBUS, ( void ( * )( int ) )critical_error_logger );
    signal( SIGINT, ( void ( * )( int ) )critical_error_logger );
    signal( SIGTERM, ( void ( * )( int ) )critical_error_logger );
    signal( SIGQUIT, ( void ( * )( int ) )critical_error_logger );
    signal( SIGABRT, ( void ( * )( int ) )critical_error_logger );

    logging( RANK, INFO, "LINE %d | Signals Setup", __LINE__ );

    root_code { mkdir( "./logs", 0777 ); }
    MPI_Barrier( MPI_COMM_WORLD );

    logging( RANK, INFO, "LINE %d | Logs Created", __LINE__ );

    if ( _debug )
        logging( RANK, INFO, "Input Path: %s", input_path );

    //=========================================================================
    //===          CÁLCULO RÍGIDO DE MEMÓRIA (MAX 80% TOTAL)                 ===
    //=========================================================================

    struct timeval algorithm_start, algorithm_end;
    const uint32_t vertex_len = _10M;
    const uint32_t edges_len = _800M;

    uint64_t global_chunk_size = 0;
    int *sendcounts = NULL;
    int *displs = NULL;
    uint32_t local_chunk_size = 0;

    root_code {
        struct sys_mem_stats sms;
        get_memory_stats( &sms );

        logging( RANK, INFO, "LINE %d | Memory Stats: %lu %lu %lu", __LINE__, sms.total, sms.free, sms.used );

        uint64_t max_allowed_mem = ( sms.total * 80 ) / 100;

        logging( RANK, INFO, "LINE %d | Max Allowed Mem: %lu", __LINE__, max_allowed_mem );

        uint64_t components_cost = (uint64_t)vertex_len * sizeof( component_t );
        uint64_t mst_cost = (uint64_t)vertex_len * sizeof( edge_t );
        uint64_t sparse_max_cost = (uint64_t)vertex_len * sizeof( sparse_msg_t );

        uint64_t fixed_costs = components_cost + mst_cost + sparse_max_cost;
        uint64_t available_for_buffers = ( max_allowed_mem > fixed_costs ) ? ( max_allowed_mem - fixed_costs ) : 100 * 1024 * 1024;

        logging( RANK, INFO, "LINE %d | Fixed Costs: %lu", __LINE__, fixed_costs );
        logging( RANK, INFO, "LINE %d | Available for Buffers: %lu", __LINE__, available_for_buffers );

        global_chunk_size = available_for_buffers / ( 2 * sizeof( edge_t ) );

        logging( RANK, INFO, "LINE %d | Global Chunk Size: %lu", __LINE__, global_chunk_size );

        if ( global_chunk_size > edges_len ) global_chunk_size = edges_len;
        if ( global_chunk_size == 0 ) global_chunk_size = 1000000;

        logging( RANK, INFO, "LINE %d | Global Chunk Size: %lu", __LINE__, global_chunk_size );

        sendcounts = malloc( SIZE * sizeof( int ) );
        displs = malloc( SIZE * sizeof( int ) );
        test( sendcounts && displs );

        logging( RANK, INFO, "LINE %d | Sendcounts and Displs Allocated", __LINE__ );

        int base_chunk = global_chunk_size / SIZE;
        int rem_chunk = global_chunk_size % SIZE;
        int offset_acc = 0;

        logging( RANK, INFO, "LINE %d | Base Chunk: %d", __LINE__, base_chunk );
        logging( RANK, INFO, "LINE %d | Rem Chunk: %d", __LINE__, rem_chunk );

        for ( int i = 0; i < SIZE; ++i ) {
            sendcounts[i] = base_chunk + ( i < rem_chunk ? 1 : 0 );
            displs[i] = offset_acc;
            offset_acc += sendcounts[i];

            logging( RANK, INFO, "LINE %d | Sendcounts[%d]: %d", __LINE__, i, sendcounts[i] );
            logging( RANK, INFO, "LINE %d | Displs[%d]: %d", __LINE__, i, displs[i] );
        }
    }

    //=========================================================================
    // *** CORREÇÃO PRINCIPAL: BROADCAST DO global_chunk_size ***
    //=========================================================================
    MPI_Bcast( &global_chunk_size, 1, MPI_UINT64_T, 0, MPI_COMM_WORLD );
    logging( RANK, INFO, "LINE %d | Global Chunk Size (after Bcast): %lu", __LINE__, global_chunk_size );

    MPI_Scatter( sendcounts, 1, MPI_INT, &local_chunk_size, 1, MPI_INT, 0, MPI_COMM_WORLD );

    logging( RANK, INFO, "LINE %d | Local Chunk Size: %u", __LINE__, local_chunk_size );

    if ( _debug )
        logging( RANK, INFO, "Local chunk size: %u", local_chunk_size );

    //=========================================================================
    //===                   ALOCAÇÕES GLOBAIS                                ===
    //=========================================================================

    component_t *components = malloc( sizeof( component_t ) * vertex_len );
    test( components != NULL );

    logging( RANK, INFO, "LINE %d | Components Allocated", __LINE__ );

    edge_t *mst = NULL;
    root_code {
        mst = malloc( sizeof( edge_t ) * vertex_len );
        test( mst != NULL );
        logging( RANK, INFO, "LINE %d | MST Allocated", __LINE__ );
    }

    gettimeofday( &algorithm_start, NULL );

    edge_t *send_work = NULL, *send_bg = NULL;
    edge_t *recv_work = NULL, *recv_bg = NULL;

    root_code {
        send_work = malloc( sizeof( edge_t ) * global_chunk_size );
        send_bg = malloc( sizeof( edge_t ) * global_chunk_size );
        test( send_work != NULL && send_bg != NULL );
        logging( RANK, INFO, "LINE %d | Send Work and Send BG Allocated", __LINE__ );
    }

    recv_work = malloc( sizeof( edge_t ) * local_chunk_size );
    recv_bg = malloc( sizeof( edge_t ) * local_chunk_size );
    test( recv_work != NULL && recv_bg != NULL );

    logging( RANK, INFO, "LINE %d | Recv Work and Recv BG Allocated", __LINE__ );

    sparse_msg_t *local_sparse = malloc( sizeof( sparse_msg_t ) * vertex_len );
    test( local_sparse != NULL );

    logging( RANK, INFO, "LINE %d | Local Sparse Allocated", __LINE__ );

    for ( uint32_t i = 0; i < vertex_len; ++i ) {
        components[i]._id = i;
        components[i].best_edge.src = INF32;
    }

    logging( RANK, INFO, "LINE %d | Components Initialized", __LINE__ );

    uint32_t num_components = vertex_len;
    uint32_t mst_size = 0;
    bool done = false;
    int pass = 1;

    //=========================================================================
    //===                   ALGORITMO DE BORUVKA                             ===
    //=========================================================================

    while ( !done && num_components > 1 ) {
        if ( _debug )
            logging( RANK, INFO, "\n--- Starting Pass %d (Components left: %u) ---", pass, num_components );

        for ( uint32_t i = 0; i < vertex_len; ++i )
            components[i].best_edge.src = INF32;

        logging( RANK, INFO, "LINE %d | Components Reset Best Edge for pass %d", __LINE__, pass );

        root_code {
            FILE *fp = fopen( input_path, "rb" );
            test_op( fp, !=, NULL );

            logging( RANK, INFO, "LINE %d | File Opened", __LINE__ );

            uint32_t edges_left = edges_len;
            uint32_t total_chunks = ( edges_len + global_chunk_size - 1 ) / global_chunk_size;

            logging( RANK, INFO, "LINE %d | Edges Left: %u, Total Chunks: %u", __LINE__, edges_left, total_chunks );

            uint32_t to_read = MIN( global_chunk_size, edges_left );
            size_t items_read = fread( send_work, sizeof( edge_t ), to_read, fp );
            test_op( items_read, ==, to_read );
            edges_left -= to_read;

            for ( uint32_t i = items_read; i < global_chunk_size; ++i ) {
                send_work[i].src = INF32;
                send_work[i].dst = INF32;
                send_work[i].w = DBL_MAX;
            }

            MPI_Scatterv( send_work, sendcounts, displs, MPI_EDGE_T,
                          recv_work, local_chunk_size, MPI_EDGE_T, 0, MPI_COMM_WORLD );

            logging( RANK, INFO, "LINE %d | First Scatterv performed", __LINE__ );

            uint32_t step = 1;
            for ( ; step <= total_chunks; ++step ) {
                bool has_next = ( step < total_chunks );

                // Processa bloco ATUAL
                for ( uint32_t i = 0; i < local_chunk_size; ++i ) {
                    if ( recv_work[i].src == INF32 ) continue;

                    const uint32_t u = find_component( recv_work[i].src, components );
                    const uint32_t v = find_component( recv_work[i].dst, components );

                    if ( u != v ) {
                        if ( preferable_over( &recv_work[i], &components[u].best_edge ) )
                            components[u].best_edge = recv_work[i];
                        if ( preferable_over( &recv_work[i], &components[v].best_edge ) )
                            components[v].best_edge = recv_work[i];
                    }
                }

                if ( has_next ) {
                    to_read = MIN( global_chunk_size, edges_left );
                    items_read = fread( send_bg, sizeof( edge_t ), to_read, fp );
                    test_op( items_read, ==, to_read );
                    edges_left -= to_read;

                    for ( uint32_t i = items_read; i < global_chunk_size; ++i ) {
                        send_bg[i].src = INF32;
                        send_bg[i].dst = INF32;
                        send_bg[i].w = DBL_MAX;
                    }

                    MPI_Scatterv( send_bg, sendcounts, displs, MPI_EDGE_T,
                                  recv_bg, local_chunk_size, MPI_EDGE_T, 0, MPI_COMM_WORLD );

                    edge_t *tmp_s = send_work; send_work = send_bg; send_bg = tmp_s;
                    edge_t *tmp_r = recv_work; recv_work = recv_bg; recv_bg = tmp_r;
                }
            }

            // *** CORREÇÃO 2: REMOVIDO processamento duplicado ***

            fclose( fp );
            logging( RANK, INFO, "LINE %d | File Closed", __LINE__ );
        }

        worker_code {
            uint32_t total_chunks = ( edges_len + global_chunk_size - 1 ) / global_chunk_size;
            logging( RANK, INFO, "LINE %d | Total chunks: %u", __LINE__, total_chunks );

            MPI_Scatterv( NULL, NULL, NULL, MPI_EDGE_T,
                          recv_work, local_chunk_size, MPI_EDGE_T, 0, MPI_COMM_WORLD );
            logging( RANK, INFO, "LINE %d | First Chunk Received", __LINE__ );

            for ( uint32_t step = 1; step <= total_chunks; ++step ) {
                bool has_next = ( step < total_chunks );

                for ( uint32_t i = 0; i < local_chunk_size; ++i ) {
                    if ( recv_work[i].src == INF32 ) continue;

                    const uint32_t u = find_component( recv_work[i].src, components );
                    const uint32_t v = find_component( recv_work[i].dst, components );

                    if ( u != v ) {
                        if ( preferable_over( &recv_work[i], &components[u].best_edge ) )
                            components[u].best_edge = recv_work[i];
                        if ( preferable_over( &recv_work[i], &components[v].best_edge ) )
                            components[v].best_edge = recv_work[i];
                    }
                }

                if ( has_next ) {
                    MPI_Scatterv( NULL, NULL, NULL, MPI_EDGE_T,
                                  recv_bg, local_chunk_size, MPI_EDGE_T, 0, MPI_COMM_WORLD );

                    edge_t *tmp_r = recv_work; recv_work = recv_bg; recv_bg = tmp_r;
                }
            }

            // *** CORREÇÃO 2: REMOVIDO processamento duplicado ***
            logging( RANK, INFO, "LINE %d | All Chunks Processed", __LINE__ );
        }

        //=====================================================================
        // FASE 2: REDUÇÃO ESPARSA
        //=====================================================================

        uint32_t local_sparse_count = 0;
        for ( uint32_t i = 0; i < vertex_len; ++i ) {
            if ( components[i].best_edge.src != INF32 ) {
                local_sparse[local_sparse_count].comp_id = i;
                local_sparse[local_sparse_count].src = components[i].best_edge.src;
                local_sparse[local_sparse_count].dst = components[i].best_edge.dst;
                local_sparse[local_sparse_count].w = components[i].best_edge.w;
                local_sparse_count++;
            }
        }

        int *sparse_counts = malloc( SIZE * sizeof( int ) );
        MPI_Allgather( &local_sparse_count, 1, MPI_INT, sparse_counts, 1, MPI_INT, MPI_COMM_WORLD );

        int *sparse_displs = malloc( SIZE * sizeof( int ) );
        int total_sparse = 0;
        for ( int i = 0; i < SIZE; ++i ) {
            sparse_displs[i] = total_sparse;
            total_sparse += sparse_counts[i];
        }

        sparse_msg_t *global_sparse = NULL;
        if ( total_sparse > 0 ) {
            global_sparse = malloc( total_sparse * sizeof( sparse_msg_t ) );
            test( global_sparse != NULL );

            MPI_Allgatherv( local_sparse, local_sparse_count, MPI_SPARSE_MSG_T,
                            global_sparse, sparse_counts, sparse_displs, MPI_SPARSE_MSG_T,
                            MPI_COMM_WORLD );
        }

        free( sparse_counts );
        free( sparse_displs );

        //=====================================================================
        // FASE 3: UNION DETERMINÍSTICO
        //=====================================================================

        for ( uint32_t i = 0; i < vertex_len; ++i )
            components[i].best_edge.src = INF32;

        for ( int i = 0; i < total_sparse; ++i ) {
            uint32_t cid = global_sparse[i].comp_id;
            edge_t e = { global_sparse[i].src, global_sparse[i].dst, global_sparse[i].w };

            if ( preferable_over( &e, &components[cid].best_edge ) )
                components[cid].best_edge = e;
        }

        if ( global_sparse ) free( global_sparse );

        uint32_t edges_added = 0;
        edge_t *pass_mst = malloc( vertex_len * sizeof( edge_t ) );
        test( pass_mst != NULL );

        for ( uint32_t i = 0; i < vertex_len; ++i ) {
            if ( components[i]._id == i && components[i].best_edge.src != INF32 ) {
                edge_t best = components[i].best_edge;
                uint32_t root_u = find_component( best.src, components );
                uint32_t root_v = find_component( best.dst, components );

                if ( root_u != root_v ) {
                    components[root_u]._id = root_v;
                    pass_mst[edges_added++] = best;
                    num_components--;
                }
            }
        }

        root_code {
            for ( uint32_t i = 0; i < edges_added; ++i )
                mst[mst_size++] = pass_mst[i];
        }

        free( pass_mst );

        if ( edges_added == 0 ) {
            done = true;
        }
        
        pass++;  // *** CORREÇÃO 3: Incremento no final ***
    }

    gettimeofday( &algorithm_end, NULL );

    //=========================================================================
    //===                   FINALIZAÇÃO E OUTPUT                             ===
    //=========================================================================

    root_code {
        printf( "TIME: %ldµs\n",
                ( algorithm_end.tv_sec - algorithm_start.tv_sec ) * 1000000 +
                    ( algorithm_end.tv_usec - algorithm_start.tv_usec ) );

        qsort( mst, mst_size, sizeof( edge_t ), compare_edges );

        FILE *fp_out = fopen( "./output_paralelo", "w" );
        if ( fp_out ) {
            double final_weight = 0.0;
            for ( uint32_t i = 0; i < mst_size; ++i ) {
                fprintf( fp_out, "%u %.12f %u\n", mst[i].src, mst[i].w, mst[i].dst );
                final_weight += mst[i].w;
            }
            printf( "FINAL WEIGHT -> %.12f\n", final_weight );
            fclose( fp_out );
        }
    }

    //=========================================================================
    //===                   LIMPEZA                                         ===
    //=========================================================================

    free( recv_work );
    free( recv_bg );
    free( local_sparse );
    free( components );

    root_code {
        free( mst );
        free( send_work );
        free( send_bg );
        free( sendcounts );
        free( displs );
    }

    MPI_Type_free( &MPI_SPARSE_MSG_T );
    MPI_Type_free( &MPI_EDGE_T );
    MPI_Finalize();

    return 0;
}

// ---------------- FUNÇÕES AUXILIARES ---------------- //

static int compare_edges( const void *a, const void *b ) {
    const edge_t *A = ( edge_t * )a;
    const edge_t *B = ( edge_t * )b;
    if ( A->w < B->w ) return -1;
    if ( A->w > B->w ) return 1;
    return 0;
}

uint32_t find_component( uint32_t vertex_id, component_t *components ) {
    uint32_t root = vertex_id;
    while ( components[root]._id != root )
        root = components[root]._id;

    uint32_t curr = vertex_id;
    while ( curr != root ) {
        uint32_t next = components[curr]._id;
        components[curr]._id = root;
        curr = next;
    }
    return root;
}

bool preferable_over( const edge_t *new_edge, const edge_t *current ) {
    if ( current->src == INF32 ) return true;
    if ( new_edge->src == INF32 ) return false;

    if ( new_edge->w < current->w ) return true;

    if ( new_edge->w == current->w ) {
        const uint32_t ns = new_edge->src < new_edge->dst ? new_edge->src : new_edge->dst;
        const uint32_t nd = new_edge->src < new_edge->dst ? new_edge->dst : new_edge->src;
        const uint32_t cs = current->src < current->dst ? current->src : current->dst;
        const uint32_t cd = current->src < current->dst ? current->dst : current->src;
        return ns < cs || ( ns == cs && nd < cd );
    }
    return false;
}

void get_memory_stats( struct sys_mem_stats *_sms ) {
    if ( !_sms ) return;
    FILE *fp = fopen( "/proc/meminfo", "r" );
    if ( fp ) {
        char line[256];
        int found = 0;
        while ( fgets( line, sizeof( line ), fp ) ) {
            if ( strncmp( line, "MemTotal:", 9 ) == 0 ) {
                sscanf( line + 9, "%lu", &_sms->total );
                _sms->total *= 1024;
                found++;
            } else if ( strncmp( line, "MemAvailable:", 13 ) == 0 ) {
                sscanf( line + 13, "%lu", &_sms->free );
                _sms->free *= 1024;
                found++;
            }
            if ( found == 2 ) break;
        }
        fclose( fp );
        _sms->used = _sms->total - _sms->free;
        return;
    }
    _sms->free = _sms->used = _sms->total = 0;
}

void critical_error_logger( int _signal ) {
    const char *msg = "UNKNOWN";
    switch ( _signal ) {
        case SIGSEGV: msg = "SEGMENTATION FAULT"; break;
        case SIGFPE:  msg = "FLOATING POINT EXCEPTION / DIVISION BY ZERO"; break;
        case SIGILL:  msg = "ILLEGAL INSTRUCTION"; break;
        case SIGBUS:  msg = "BUS ERROR"; break;
        default:      msg = "SIGNAL CAUGHT"; break;
    }
    logging( *_log_error_rank, CRITICAL_ERROR, "CODE FAILURE... %s", msg );
    MPI_Abort( MPI_COMM_WORLD, 1 );
    exit( 1 );
}

void logging( int _rank, log_level_t _level, const char *_fmt, ... ) {
    struct timeval time;
    gettimeofday( &time, NULL );
    time.tv_sec = time.tv_sec - _t0.tv_sec;
    time.tv_usec = time.tv_usec - _t0.tv_usec;
    if ( time.tv_usec < 0 ) {
        time.tv_usec += 1000000;
        time.tv_sec--;
    }

    char filename[256];
    snprintf( filename, sizeof( filename ), "./logs/rank%d.log", _rank );

    FILE *fp = fopen( filename, "a+" );
    if ( !fp ) return;

    const char *lvl = "INFO";
    if ( _level == WARNING ) lvl = "WARNING";
    else if ( _level == ERROR ) lvl = "ERROR";
    else if ( _level == CRITICAL_ERROR ) lvl = "CRITICAL ERROR";

    char prefix[256];
    snprintf( prefix, sizeof( prefix ), "[%ld.%06ld] RANK %d | [%s] --- ", time.tv_sec, time.tv_usec, _rank, lvl );

    char content[2048];
    va_list args;
    va_start( args, _fmt );
    vsnprintf( content, sizeof( content ), _fmt, args );
    va_end( args );

    fprintf( fp, "%s%s\n", prefix, content );
    fclose( fp );
}