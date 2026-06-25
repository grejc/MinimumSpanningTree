// 75117.848736822314
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
#define RESET_EDGE( e ) ( e.src = INF32, e.dst = INF32, e.w = DBL_MAX )
#define ROOT ( *_RANK == 0 )

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

struct sys_mem_stats {
    uint64_t total;
    uint64_t free;
    uint64_t used;
};

typedef enum { INFO, SEND, RECV, WARNING, ERROR, CRITICAL_ERROR } log_level_t;
//-----------------------------------------------------------------------------

//=============================================================================
//===                                 GLOBAIS                               ===
//=============================================================================

static int *_RANK, *_SIZE;
static int *_log_error_rank;
static struct timeval _t0;
//-----------------------------------------------------------------------------

//=============================================================================
//===                          DEFINIÇÃO DE FUNÇÕES                         ===
//=============================================================================

int compare_edges( const void *a, const void *b );
void get_memory_stats( struct sys_mem_stats *_sms );
void reduce_best_edge_operation( void *invec, void *inoutvec, int *len, MPI_Datatype *datatype );
uint32_t find_component( uint32_t vertex_id, component_t *components );
void critical_error_logger( int _signal );
void logging( int _rank, log_level_t _level, const char *_fmt, ... );
int can_allocate( size_t _len, size_t _size );

//-----------------------------------------------------------------------------

void __startup__( void ) __attribute__( ( constructor ) );
void __startup__() { gettimeofday( &_t0, NULL ); }

static inline double get_elapsed( struct timeval t0, struct timeval t1 ) {
    return ( t1.tv_sec - t0.tv_sec ) + ( t1.tv_usec - t0.tv_usec ) / 1e6;
}

int main( int argc, char **argv ) {
    // Capture start time
    struct timeval t_start, t_io_sort, t_loop, t_end;
    gettimeofday( &t_start, NULL );

    //=========================================================================
    //===                      INICIALIZANDO MPI                            ===
    //=========================================================================
    MPI_Init( &argc, &argv );
    int RANK, SIZE;

    _RANK = &RANK, _SIZE = &SIZE;

    MPI_Comm_rank( MPI_COMM_WORLD, &RANK ), MPI_Comm_size( MPI_COMM_WORLD, &SIZE );

    // DEFINIÇÃO DE TIPOS CUSTOMIZADOS MPI
    MPI_Datatype MPI_EDGE_T;
    int MPI_EDGE_T_blocklen[] = { 1, 1, 1 };
    MPI_Datatype MPI_EDGE_T_types[] = { MPI_UINT32_T, MPI_UINT32_T, MPI_DOUBLE };
    MPI_Aint MPI_EDGE_T_offsets[] = { offsetof( edge_t, src ), offsetof( edge_t, dst ), offsetof( edge_t, w ) };
    MPI_Type_create_struct( 3, MPI_EDGE_T_blocklen, MPI_EDGE_T_offsets, MPI_EDGE_T_types, &MPI_EDGE_T );
    MPI_Type_commit( &MPI_EDGE_T );

    MPI_Datatype MPI_COMPONENT_T;
    int MPI_COMPONENT_T_blocklen[] = { 1, 1 };
    MPI_Datatype MPI_COMPONENT_T_types[] = { MPI_UINT32_T, MPI_EDGE_T };
    MPI_Aint MPI_COMPONENT_T_offsets[] = { offsetof( component_t, _id ), offsetof( component_t, best_edge ) };
    MPI_Type_create_struct( 2, MPI_COMPONENT_T_blocklen, MPI_COMPONENT_T_offsets, MPI_COMPONENT_T_types,
                            &MPI_COMPONENT_T );
    MPI_Type_commit( &MPI_COMPONENT_T );

    MPI_Datatype MPI_MEMINFO_T;
    int MPI_MEMINFO_T_blocklen[] = { 1, 1, 1 };
    MPI_Datatype MPI_MEMINFO_T_types[] = { MPI_UINT64_T, MPI_UINT64_T, MPI_UINT64_T };
    MPI_Aint MPI_MEMINFO_T_offsets[] = { offsetof( struct sys_mem_stats, total ),
                                         offsetof( struct sys_mem_stats, used ),
                                         offsetof( struct sys_mem_stats, free ) };
    MPI_Type_create_struct( 3, MPI_MEMINFO_T_blocklen, MPI_MEMINFO_T_offsets, MPI_MEMINFO_T_types, &MPI_MEMINFO_T );
    MPI_Type_commit( &MPI_MEMINFO_T );

    // DEFINIÇÃO DE OPERAÇÃO CUSTOMIZADAS
    MPI_Op MPI_BEST_EDGE_OP;
    MPI_Op_create( reduce_best_edge_operation, true, &MPI_BEST_EDGE_OP );

    //=========================================================================
    //===                   INICIALIZANDO LOGGERS                            ===
    //=========================================================================
    _log_error_rank = &RANK;

    signal( SIGSEGV, (void ( * )( int ))critical_error_logger );
    signal( SIGFPE, (void ( * )( int ))critical_error_logger );
    signal( SIGILL, (void ( * )( int ))critical_error_logger );
    signal( SIGBUS, (void ( * )( int ))critical_error_logger );
    signal( SIGINT, (void ( * )( int ))critical_error_logger );
    signal( SIGTERM, (void ( * )( int ))critical_error_logger );
    signal( SIGQUIT, (void ( * )( int ))critical_error_logger );
    signal( SIGABRT, (void ( * )( int ))critical_error_logger );
    signal( SIGHUP, (void ( * )( int ))critical_error_logger );
    signal( SIGCHLD, (void ( * )( int ))critical_error_logger );
    signal( SIGPIPE, (void ( * )( int ))critical_error_logger );

    // Cria diretório de logs se necessário
    if ( ROOT ) {
        mkdir( "./logs", 0777 );
    }
    MPI_Barrier( MPI_COMM_WORLD );

    //=========================================================================
    //===                PARSING DE PARÂMETROS COM ARGPARSER                ===
    //=========================================================================
    char *input_path = NULL, *output_path = NULL;
    int verbosity = 1;

    if ( ROOT ) {
        ArgParser ap;
        argp_init( &ap, "JoseCarlos_RGM42426_AGMpar", "1.1.0",
                   "Algoritmo Paralelo (MPI) para calcular a Arvore Geradora Minima "
                   "(MST)" );
        argp_add_option( &ap, 'v', "verbose", "LEVEL", "Nivel de verbosidade: 0 (quiet), 1 (normal), 2 (debug/verbose)",
                         "1" );
        argp_add_option( &ap, 'o', "output", "FILE", "Arquivo de saida para o MST",
                         "./JoseCarlos_RGM42426_AGMpar_Saida.txt" );
        argp_add_pos( &ap, "input", "Caminho do arquivo binario de entrada", 1 );

        if ( !argp_parse( &ap, argc, argv ) ) {
            argp_print_error( &ap );
            MPI_Finalize();
            return 1;
        }

        input_path = strdup( argp_pos( &ap, "input" ) );
        output_path = strdup( argp_get( &ap, "output" ) );

        const char *v_str = argp_get( &ap, "verbose" );
        if ( v_str ) {
            verbosity = atoi( v_str );
        }

        argp_free( &ap );
    }

    logging( RANK, INFO, "Processo inicializado no comunicador MPI com rank %d de %d", RANK, SIZE );
    logging( RANK, INFO, "Parametros do Parser: input='%s', verbosity=%d", input_path, verbosity );

    if ( ROOT && verbosity >= 1 ) {
        print_box_double( "ALGORITMO PARALELO (MPI)", TERMINAL_COLOR_BLUE );
    }

    //=========================================================================
    //===                     DEFINIÇÃO DE METADADOS                        ===
    //=========================================================================
    uint32_t vertex_len = _10M;
    uint32_t edges_len = _800M;
    uint32_t processed_edges = 0;
    uint32_t edges_to_process_len = 0;
    edge_t *edges_to_process = NULL;
    component_t *components_global;
    component_t *components_local;

    // Candidatos da MST: acumula arestas de todos os lotes (ROOT aloca grande)
    // Cada lote pode contribuir ate V-1 arestas. Numero de lotes = edges_len / (SIZE * _10M).
    // Maximo teorico: num_lotes * (V-1), mas na pratica muito menos.
    edge_t *E_line = NULL;
    size_t m = 0;
    size_t components_len = 0;
    uint32_t num_lotes = edges_len / ( (uint32_t)SIZE * _10M );
    size_t e_line_capacity = (size_t)num_lotes * ( vertex_len - 1 );

    logging( RANK, INFO, "Estruturas e metadados iniciais definidos. Lotes previstos: %u", num_lotes );

    //=========================================================================
    //===               INICIALIZAÇÃO COMPONENTES GLOBAIS                   ===
    //=========================================================================
    // Lista de componentes globais
    components_global = malloc( sizeof( component_t ) * vertex_len );
    if ( components_global == NULL ) {
        logging( RANK, ERROR, "Erro ao alocar memoria para a lista de componentes" );
        print_error( "Erro de alocacao", "Erro ao alocar memoria para a lista de componentes", NULL, -1 );
        MPI_Finalize();
        return 1;
    }

    // Lista de componentes locais — alocado UMA VEZ fora do loop
    components_local = malloc( sizeof( component_t ) * vertex_len );
    if ( components_local == NULL ) {
        logging( RANK, ERROR, "Erro ao alocar memoria para componentes locais" );
        free( components_global );
        MPI_Finalize();
        return 1;
    }

    // Buffer de arestas para processar — alocado UMA VEZ, reutilizado a cada lote
    edges_to_process = malloc( sizeof( edge_t ) * _10M );
    if ( edges_to_process == NULL ) {
        logging( RANK, ERROR, "Erro ao alocar memoria para edges_to_process" );
        free( components_global );
        free( components_local );
        MPI_Finalize();
        return 1;
    }
    edges_to_process_len = _10M;

    // Candidatos da MST (apenas ROOT armazena as arestas candidatas de cada lote)
    if ( ROOT ) {
        E_line = malloc( sizeof( edge_t ) * e_line_capacity );
        if ( E_line == NULL ) {
            logging( RANK, ERROR, "Erro ao alocar memoria para E_line (candidatos MST)" );
            free( components_global );
            free( components_local );
            free( edges_to_process );
            MPI_Finalize();
            return 1;
        }
    }

    logging( RANK, INFO, "Componentes e buffers alocados." );

    //=========================================================================
    //===             FASE 1: BORŮVKA INDEPENDENTE POR LOTE                 ===
    //===   Cada lote roda Borůvka com Union-Find fresco, coletando        ===
    //===   arestas candidatas em E_line. Pela propriedade do corte,       ===
    //===   MST(G) = MST( ∪ MST(Eᵢ) ).                                    ===
    //=========================================================================
    FILE *f_in = NULL;

    if ( ROOT ) {
        f_in = fopen( input_path, "rb" );
        test( f_in != NULL );
        logging( RANK, INFO, "Arquivo de entrada aberto com sucesso." );
    }

    // Captura o término da fase de I/O e início do processamento
    gettimeofday( &t_io_sort, NULL );

    while ( processed_edges < edges_len ) {
        logging( RANK, INFO, "[FASE 1] Iniciando lote. Arestas processadas: %u / %u", processed_edges, edges_len );

        // --------------------------LEITURA E ENVIO-------------------------//
        if ( ROOT ) {
            edge_t *send_buffer = (edge_t *)malloc( sizeof( edge_t ) * _10M );
            test( send_buffer );

            for ( int i = 0; i < SIZE; i++ ) {
                size_t read_bytes = fread( send_buffer, sizeof( edge_t ), _10M, f_in );
                (void)read_bytes;
                logging( RANK, INFO, "Lendo bloco de arestas no Rank 0 para o Rank %d.", i );

                if ( i == 0 ) {
                    memcpy( edges_to_process, send_buffer, sizeof( edge_t ) * _10M );
                } else {
                    MPI_Send( send_buffer, _10M, MPI_EDGE_T, i, 0, MPI_COMM_WORLD );
                }
            }

            free( send_buffer );
        } else {
            MPI_Recv( edges_to_process, _10M, MPI_EDGE_T, 0, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE );
        }

        processed_edges += (uint32_t)SIZE * _10M;

        // ---------- BORŮVKA INDEPENDENTE PARA ESTE LOTE ----------//
        // Resetar Union-Find: cada lote começa com todos os vertices como singletons
        for ( size_t i = 0; i < vertex_len; ++i ) {
            components_local[i]._id = i;
            RESET_EDGE( components_local[i].best_edge );
        }
        components_len = vertex_len;

        bool done = false;
        while ( !done ) {
            // 1 - Resetar melhores arestas
            for ( size_t i = 0; i < vertex_len; ++i ) {
                RESET_EDGE( components_local[i].best_edge );
            }

            // 2 - Busca local
            for ( size_t i = 0; i < edges_to_process_len; ++i ) {
                uint32_t u = edges_to_process[i].src;
                uint32_t v = edges_to_process[i].dst;
                double w = edges_to_process[i].w;

                uint32_t raiz_u = find_component( u, components_local );
                uint32_t raiz_v = find_component( v, components_local );

                if ( raiz_u != raiz_v ) {
                    if ( components_local[raiz_u].best_edge.src == INF32 || components_local[raiz_u].best_edge.w > w ) {
                        components_local[raiz_u].best_edge = edges_to_process[i];
                    }
                    if ( components_local[raiz_v].best_edge.src == INF32 || components_local[raiz_v].best_edge.w > w ) {
                        components_local[raiz_v].best_edge = edges_to_process[i];
                    }
                }
            }

            // 3 - Allreduce: consolidar melhores arestas globalmente
            MPI_Allreduce( components_local, components_global, vertex_len, MPI_COMPONENT_T, MPI_BEST_EDGE_OP,
                           MPI_COMM_WORLD );

            // 4 - Fusão de componentes e coleta de candidatos
            bool arestas_adicionadas = false;
            for ( size_t i = 0; i < vertex_len; ++i ) {
                if ( components_global[i].best_edge.src != INF32 ) {
                    edge_t escolhida = components_global[i].best_edge;
                    uint32_t raiz_u = find_component( escolhida.src, components_local );
                    uint32_t raiz_v = find_component( escolhida.dst, components_local );

                    if ( raiz_u != raiz_v ) {
                        // ROOT coleta a aresta candidata
                        if ( ROOT ) {
                            E_line[m++] = escolhida;
                        }
                        components_local[raiz_u]._id = raiz_v;
                        components_len--;
                        arestas_adicionadas = true;
                    }
                }
            }

            // 5 - Parada
            if ( !arestas_adicionadas || components_len == 1 ) {
                done = true;
            }
        }
        logging( RANK, INFO, "[FASE 1] Lote concluido. Candidatos acumulados: %zu", m );
    }
    if ( ROOT ) {
        fclose( f_in );
    }
    logging( RANK, INFO, "[FASE 1] Concluida. Total de arestas candidatas coletadas: %zu", m );

    //=========================================================================
    //===        FASE 2: BORŮVKA FINAL SOBRE ARESTAS CANDIDATAS             ===
    //===   Broadcast de E_line para todos, distribuir entre ranks,         ===
    //===   rodar Borůvka final para obter a MST verdadeira.                ===
    //=========================================================================
    if ( ROOT ) {
        logging( RANK, INFO, "[FASE 2] Iniciando Borůvka sequencial no ROOT sobre %zu candidatos.", m );

        // Resetar Union-Find para a fase final
        for ( size_t i = 0; i < vertex_len; ++i ) {
            components_local[i]._id = i;
            RESET_EDGE( components_local[i].best_edge );
        }
        components_len = vertex_len;

        edge_t *mst_final = malloc( sizeof( edge_t ) * ( vertex_len - 1 ) );
        test( mst_final );
        size_t mst_count = 0;

        bool done = false;
        int iteracao = 0;
        while ( !done ) {
            iteracao++;
            logging( RANK, INFO, "[FASE 2] Iteracao %d. Componentes restantes: %zu", iteracao, components_len );

            // 1 - Resetar melhores arestas
            for ( size_t i = 0; i < vertex_len; ++i ) {
                RESET_EDGE( components_local[i].best_edge );
            }

            // 2 - Busca sequencial sobre E_line
            for ( size_t i = 0; i < m; ++i ) {
                uint32_t u = E_line[i].src;
                uint32_t v = E_line[i].dst;
                double w = E_line[i].w;

                uint32_t raiz_u = find_component( u, components_local );
                uint32_t raiz_v = find_component( v, components_local );

                if ( raiz_u != raiz_v ) {
                    if ( components_local[raiz_u].best_edge.src == INF32 || components_local[raiz_u].best_edge.w > w ) {
                        components_local[raiz_u].best_edge = E_line[i];
                    }
                    if ( components_local[raiz_v].best_edge.src == INF32 || components_local[raiz_v].best_edge.w > w ) {
                        components_local[raiz_v].best_edge = E_line[i];
                    }
                }
            }

            // 3 - Fusão
            bool arestas_adicionadas = false;
            for ( size_t i = 0; i < vertex_len; ++i ) {
                if ( components_local[i].best_edge.src != INF32 ) {
                    edge_t escolhida = components_local[i].best_edge;
                    uint32_t raiz_u = find_component( escolhida.src, components_local );
                    uint32_t raiz_v = find_component( escolhida.dst, components_local );

                    if ( raiz_u != raiz_v ) {
                        mst_final[mst_count++] = escolhida;
                        components_local[raiz_u]._id = raiz_v;
                        components_len--;
                        arestas_adicionadas = true;
                    }
                }
            }

            // 4 - Parada
            if ( !arestas_adicionadas || components_len == 1 ) {
                done = true;
            }
        }

        logging( RANK, INFO, "[FASE 2] Concluida. MST final possui %zu arestas.", mst_count );

        // Substituir E_line pelo resultado final para a apresentacao
        free( E_line );
        E_line = mst_final;
        m = mst_count;
    }

    // Todos os ranks sincronizam antes de calcular os tempos
    MPI_Barrier( MPI_COMM_WORLD );

    // Captura o término do loop de processamento
    gettimeofday( &t_loop, NULL );

    //=========================================================================
    //===                     APRESENTAÇÃO DE RESULTADOS                    ===
    //=========================================================================
    // Cálculo de tempos decorridos
    double time_io = get_elapsed( t_start, t_io_sort );
    double time_process = get_elapsed( t_io_sort, t_loop );
    double time_total = get_elapsed( t_start, t_loop );

    logging( RANK, INFO,
             "Tempos de execucao: I/O/Setup=%f s, "
             "Processamento=%f s, Total=%f s",
             time_io, time_process, time_total );

    // Cálculo do peso total da MST
    double peso_total = 0.0;
    if ( ROOT && E_line ) {
        for ( size_t i = 0; i < m; ++i ) {
            peso_total += E_line[i].w;
        }
    }

    // Redução dos tempos para exibir mínimo e máximo entre os ranks
    double max_time_io = 0.0, min_time_io = 0.0;
    double max_time_process = 0.0, min_time_process = 0.0;

    MPI_Reduce( &time_io, &max_time_io, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD );
    MPI_Reduce( &time_io, &min_time_io, 1, MPI_DOUBLE, MPI_MIN, 0, MPI_COMM_WORLD );

    MPI_Reduce( &time_process, &max_time_process, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD );
    MPI_Reduce( &time_process, &min_time_process, 1, MPI_DOUBLE, MPI_MIN, 0, MPI_COMM_WORLD );

    // Apresentação de saídas dependente da verbosidade no terminal do Rank 0
    if ( ROOT ) {
        if ( verbosity >= 1 ) {
            print_box_double( "ARVORE GERADORA MINIMA (MST) ENCONTRADA", TERMINAL_COLOR_GREEN );

            char info_text[512];
            snprintf( info_text, sizeof( info_text ), "QUANTIDADE DE ARESTAS NA MST: %zu  |  PESO TOTAL DA MST: %.9f",
                      m, peso_total );
            print_box( info_text, TERMINAL_COLOR_CYAN );

            // Tabela de arestas (se o grafo for pequeno)
            if ( vertex_len < 100 && E_line ) {
                TableStyle ts;
                table_init( &ts, 3, TERMINAL_COLOR_CYAN, TERMINAL_COLOR_WHITE, TERMINAL_COLOR_GREEN );
                table_set_col_width( &ts, 0, 15 );
                table_set_col_width( &ts, 1, 15 );
                table_set_col_width( &ts, 2, 15 );

                const char *headers[] = { "Origem (src)", "Peso (w)", "Destino (dst)" };
                table_print_header( &ts, headers );

                for ( size_t i = 0; i < m; ++i ) {
                    char src_buf[32], w_buf[32], dst_buf[32];
                    snprintf( src_buf, sizeof( src_buf ), "%u", E_line[i].src );
                    snprintf( w_buf, sizeof( w_buf ), "%.2f", E_line[i].w );
                    snprintf( dst_buf, sizeof( dst_buf ), "%u", E_line[i].dst );
                    const char *row[] = { src_buf, w_buf, dst_buf };
                    table_print_row( &ts, row );
                }
                table_print_footer( &ts );

                // Exibe o caminho da árvore
                printf( "Caminho da arvore:\n  " );
                for ( size_t i = 0; i < m; ++i ) {
                    if ( i > 0 ) {
                        printf( " %s\u2192%s ", TERMINAL_COLOR_YELLOW, TERMINAL_COLOR_RESET );
                    }
                    printf( "%u %s(%.2f)%s %u", E_line[i].src, TERMINAL_COLOR_GREEN, E_line[i].w, TERMINAL_COLOR_RESET,
                            E_line[i].dst );
                }
                printf( "\n\n" );
            }

            // Tabela de tempos de execução
            TableStyle ts_time;
            table_init( &ts_time, 4, TERMINAL_COLOR_BLUE, TERMINAL_COLOR_WHITE, TERMINAL_COLOR_CYAN );
            table_set_col_width( &ts_time, 0, 30 );
            table_set_col_width( &ts_time, 1, 15 );
            table_set_col_width( &ts_time, 2, 15 );
            table_set_col_width( &ts_time, 3, 15 );

            const char *time_headers[] = { "Etapa do Algoritmo", "Rank 0 (s)", "Min Ranks (s)", "Max Ranks (s)" };
            table_print_header( &ts_time, time_headers );

            char r0_buf[32], min_buf[32], max_buf[32];

            snprintf( r0_buf, sizeof( r0_buf ), "%.6f", time_io );
            snprintf( min_buf, sizeof( min_buf ), "%.6f", min_time_io );
            snprintf( max_buf, sizeof( max_buf ), "%.6f", max_time_io );
            const char *row_io[] = { "1. Setup / I/O", r0_buf, min_buf, max_buf };
            table_print_row( &ts_time, row_io );

            snprintf( r0_buf, sizeof( r0_buf ), "%.6f", time_process );
            snprintf( min_buf, sizeof( min_buf ), "%.6f", min_time_process );
            snprintf( max_buf, sizeof( max_buf ), "%.6f", max_time_process );
            const char *row_proc[] = { "2. Processamento MST", r0_buf, min_buf, max_buf };
            table_print_row( &ts_time, row_proc );

            snprintf( r0_buf, sizeof( r0_buf ), "%.6f", time_total );
            const char *row_total[] = { "Total Geral Executado", r0_buf, "-", "-" };
            table_print_row( &ts_time, row_total );

            table_print_footer( &ts_time );

            print_success( NULL, "Arvore Geradora Minima (MST) calculada com sucesso!" );
        } else {
            // verbosity == 0 (quiet mode)
            printf( "MST: arestas=%zu, peso=%.15f, tempo=%.6fs\n", m, peso_total, time_total );
        }

        if ( output_path ) {
            MPI_File f_out;
            int open_ret =
                MPI_File_open( MPI_COMM_SELF, output_path, MPI_MODE_CREATE | MPI_MODE_WRONLY, MPI_INFO_NULL, &f_out );
            if ( open_ret == MPI_SUCCESS ) {
                MPI_File_set_size( f_out, 0 ); // Trunca o arquivo
                for ( size_t i = 0; i < m; ++i ) {
                    char buffer[256];
                    int len = snprintf( buffer, sizeof( buffer ), "%u %.12f %u\n", E_line[i].src, E_line[i].w,
                                        E_line[i].dst );
                    MPI_File_write( f_out, buffer, len, MPI_CHAR, MPI_STATUS_IGNORE );
                }
                MPI_File_close( &f_out );
                logging( RANK, INFO, "MST gravada com sucesso em %s", output_path );
            } else {
                logging( RANK, ERROR, "Falha ao abrir %s para gravacao", output_path );
            }
        }
    } // //=========================================================================
    // //===                     APRESENTAÇÃO DE RESULTADOS                    ===
    // //=========================================================================
    // // Cálculo de tempos decorridos
    // double time_lb = get_elapsed( t_start, t_meta_lb );
    // double time_io = get_elapsed( t_meta_lb, t_io_sort );
    // double time_process = get_elapsed( t_io_sort, t_loop );
    // double time_total = get_elapsed( t_start, t_end );

    // logging( RANK, INFO,
    //          "Tempos de execucao: Setup=%f s, I/O/Ordenacao=%f s, "
    //          "Processamento=%f s, Total=%f s",
    //          time_lb, time_io, time_process, time_total );

    // // Redução dos tempos para exibir mínimo e máximo entre os ranks
    // double max_time_lb = 0.0, min_time_lb = 0.0;
    // double max_time_io = 0.0, min_time_io = 0.0;
    // double max_time_process = 0.0, min_time_process = 0.0;

    // MPI_Reduce( &time_lb, &max_time_lb, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD );
    // MPI_Reduce( &time_lb, &min_time_lb, 1, MPI_DOUBLE, MPI_MIN, 0, MPI_COMM_WORLD );

    // MPI_Reduce( &time_io, &max_time_io, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD );
    // MPI_Reduce( &time_io, &min_time_io, 1, MPI_DOUBLE, MPI_MIN, 0, MPI_COMM_WORLD );

    // MPI_Reduce( &time_process, &max_time_process, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD );
    // MPI_Reduce( &time_process, &min_time_process, 1, MPI_DOUBLE, MPI_MIN, 0, MPI_COMM_WORLD );

    // // Apresentação de saídas dependente da verbosidade no terminal do Rank 0
    // if ( ROOT ) {
    //     if ( verbosity >= 1 ) {
    //         print_box_double( "ARVORE GERADORA MINIMA (MST) ENCONTRADA", TERMINAL_COLOR_GREEN );

    //         char info_text[512];
    //         snprintf( info_text, sizeof( info_text ), "QUANTIDADE DE ARESTAS NA MST: %d  |  PESO TOTAL DA MST: %.9f",
    //                   mst_quantidade_arestas, peso_total );
    //         print_box( info_text, TERMINAL_COLOR_CYAN );

    //         // Tabela de arestas (se o grafo for pequeno)
    //         if ( vertex_len < 100 && mst_arestas ) {
    //             TableStyle ts;
    //             table_init( &ts, 3, TERMINAL_COLOR_CYAN, TERMINAL_COLOR_WHITE, TERMINAL_COLOR_GREEN );
    //             table_set_col_width( &ts, 0, 15 );
    //             table_set_col_width( &ts, 1, 15 );
    //             table_set_col_width( &ts, 2, 15 );

    //             const char *headers[] = { "Origem (src)", "Peso (w)", "Destino (dst)" };
    //             table_print_header( &ts, headers );

    //             for ( int i = 0; i < mst_quantidade_arestas; ++i ) {
    //                 char src_buf[32], w_buf[32], dst_buf[32];
    //                 snprintf( src_buf, sizeof( src_buf ), "%u", mst_arestas[i].src );
    //                 snprintf( w_buf, sizeof( w_buf ), "%.2f", mst_arestas[i].w );
    //                 snprintf( dst_buf, sizeof( dst_buf ), "%u", mst_arestas[i].dst );
    //                 const char *row[] = { src_buf, w_buf, dst_buf };
    //                 table_print_row( &ts, row );
    //             }
    //             table_print_footer( &ts );

    //             // Exibe o caminho da árvore
    //             printf( "Caminho da arvore:\n  " );
    //             for ( int i = 0; i < mst_quantidade_arestas; ++i ) {
    //                 if ( i > 0 ) {
    //                     printf( " %s\u2192%s ", TERMINAL_COLOR_YELLOW, TERMINAL_COLOR_RESET );
    //                 }
    //                 printf( "%u %s(%.2f)%s %u", mst_arestas[i].src, TERMINAL_COLOR_GREEN, mst_arestas[i].w,
    //                         TERMINAL_COLOR_RESET, mst_arestas[i].dst );
    //             }
    //             printf( "\n\n" );
    //         }

    //         // Tabela de tempos de execução
    //         TableStyle ts_time;
    //         table_init( &ts_time, 4, TERMINAL_COLOR_BLUE, TERMINAL_COLOR_WHITE, TERMINAL_COLOR_CYAN );
    //         table_set_col_width( &ts_time, 0, 30 );
    //         table_set_col_width( &ts_time, 1, 15 );
    //         table_set_col_width( &ts_time, 2, 15 );
    //         table_set_col_width( &ts_time, 3, 15 );

    //         const char *time_headers[] = { "Etapa do Algoritmo", "Rank 0 (s)", "Min Ranks (s)", "Max Ranks (s)" };
    //         table_print_header( &ts_time, time_headers );

    //         char r0_buf[32], min_buf[32], max_buf[32];

    //         snprintf( r0_buf, sizeof( r0_buf ), "%.6f", time_lb );
    //         snprintf( min_buf, sizeof( min_buf ), "%.6f", min_time_lb );
    //         snprintf( max_buf, sizeof( max_buf ), "%.6f", max_time_lb );
    //         const char *row_lb[] = { "1. Setup / Balanc. Carga", r0_buf, min_buf, max_buf };
    //         table_print_row( &ts_time, row_lb );

    //         snprintf( r0_buf, sizeof( r0_buf ), "%.6f", time_io );
    //         snprintf( min_buf, sizeof( min_buf ), "%.6f", min_time_io );
    //         snprintf( max_buf, sizeof( max_buf ), "%.6f", max_time_io );
    //         const char *row_io[] = { "2. Leitura E/S & Ordenacao", r0_buf, min_buf, max_buf };
    //         table_print_row( &ts_time, row_io );

    //         snprintf( r0_buf, sizeof( r0_buf ), "%.6f", time_process );
    //         snprintf( min_buf, sizeof( min_buf ), "%.6f", min_time_process );
    //         snprintf( max_buf, sizeof( max_buf ), "%.6f", max_time_process );
    //         const char *row_proc[] = { "3. Processamento MST", r0_buf, min_buf, max_buf };
    //         table_print_row( &ts_time, row_proc );

    //         snprintf( r0_buf, sizeof( r0_buf ), "%.6f", time_total );
    //         const char *row_total[] = { "Total Geral Executado", r0_buf, "-", "-" };
    //         table_print_row( &ts_time, row_total );

    //         table_print_footer( &ts_time );

    //         print_success( NULL, "Arvore Geradora Minima (MST) calculada com sucesso!" );
    //     } else {
    //         // verbosity == 0 (quiet mode) - only print final MST details in a single
    //         // plain line to stdout
    //         printf( "MST: arestas=%d, peso=%.9f, tempo=%.6fs\n", mst_quantidade_arestas, peso_total, time_total );
    //     }

    //     if ( mst_arestas )
    //         free( mst_arestas );
    // }

    // Captura o tempo total de execução final
    gettimeofday( &t_end, NULL );
    logging( RANK, INFO, "Tempo total de execucao (incluindo apresentacao): %f s", get_elapsed( t_start, t_end ) );

    //=========================================================================
    //===                               FREE                                ===
    //=========================================================================
    free( components_global );
    free( components_local );
    free( edges_to_process );
    if ( ROOT && E_line ) {
        free( E_line );
    }
    if ( input_path ) {
        free( input_path );
    }
    if ( output_path ) {
        free( output_path );
    }

    MPI_Op_free( &MPI_BEST_EDGE_OP );
    MPI_Type_free( &MPI_EDGE_T );
    MPI_Type_free( &MPI_COMPONENT_T );
    MPI_Type_free( &MPI_MEMINFO_T );

    logging( RANK, INFO, "Liberacao de memoria concluida!" );
    logging( RANK, INFO, "FINALIZANDO MPI..." );
    logging( RANK, INFO, "--------------------------------------------------" );

    MPI_Finalize();
    return 0;
}

void get_memory_stats( struct sys_mem_stats *_sms ) {
    if ( !_sms )
        return;

    struct sysinfo si;

    if ( sysinfo( &si ) == 0 ) {
        _sms->total = (uint64_t)si.totalram * si.mem_unit;
        _sms->free = (uint64_t)si.freeram * si.mem_unit;
        _sms->used = _sms->total - _sms->free;

        return;
    }

    // Fallback: Parse /proc/meminfo
    FILE *fp = fopen( "/proc/meminfo", "r" );
    if ( fp ) {
        char line[256];
        int found = 0;
        while ( fgets( line, sizeof( line ), fp ) ) {
            if ( strncmp( line, "MemTotal:", 9 ) == 0 ) {
                sscanf( line + 9, "%lu", &_sms->total );
                _sms->total *= 1024; // convert kB to bytes
                found++;
            } else if ( strncmp( line, "MemFree:", 8 ) == 0 ) {
                sscanf( line + 8, "%lu", &_sms->free );
                _sms->free *= 1024; // convert kB to bytes
                found++;
            }
            if ( found == 2 )
                break;
        }
        fclose( fp );
        _sms->used = _sms->total - _sms->free;

        return;
    }

    _sms->free = _sms->used = _sms->total = 0;
}

int compare_edges( const void *a, const void *b ) {
    const edge_t *edgeA = (const edge_t *)a;
    const edge_t *edgeB = (const edge_t *)b;

    if ( edgeA->src < edgeB->src )
        return -1;
    if ( edgeA->src > edgeB->src )
        return 1;

    if ( edgeA->w < edgeB->w )
        return -1;
    if ( edgeA->w > edgeB->w )
        return 1;

    return 0;
}

void reduce_best_edge_operation( void *invec, void *inoutvec, int *len, MPI_Datatype *datatype ) {
    component_t *in = (component_t *)invec;
    component_t *inout = (component_t *)inoutvec;

    for ( int i = 0; i < *len; ++i ) {
        if ( in[i].best_edge.w < inout[i].best_edge.w ) {
            inout[i] = in[i];
        }
    }
}

uint32_t find_component( uint32_t vertex_id, component_t *components ) {
    if ( components[vertex_id]._id == vertex_id ) {
        return vertex_id;
    }
    components[vertex_id]._id = find_component( components[vertex_id]._id, components );
    return components[vertex_id]._id;
}

int can_allocate( size_t _len, size_t _size ) {
    if ( _len <= 0 || _size <= 0 )
        return -1; // How this is possible? Its literelly UNSIGNED, just don't be
                   // stupid to pass 0, but... If a programmer creates an
                   // idiot-proof system, the universe creates an idiot who will
                   // crash the system
    struct sys_mem_stats sms;

    get_memory_stats( &sms );

    uint64_t t = sms.total * 0.8;
    uint64_t free_mem = ( t > sms.used ) ? ( t - sms.used ) : 0;
    uint64_t n = free_mem / _size;
    size_t cost = _len * _size;

    if ( cost <= n ) {
        if ( _len > 2147483647 ) {
            return 2147483647;
        }
        return (int)_len;
    } else if ( n > 0 ) {
        if ( n > 2147483647 ) {
            return 2147483647;
        }
        return (int)n;
    }

    return 0; // OMG, do you colou chiclete na cruz nigga? You cant allocate
              // nothing
}

void critical_error_logger( int _signal ) {
    switch ( _signal ) {
    case SIGSEGV: {
        logging( *_log_error_rank, CRITICAL_ERROR, "CODE FAILURE...SEGMENTATION VIOLATION (CORE DUMP)\n\n" );
    } break;
    case SIGFPE: {
        logging( *_log_error_rank, CRITICAL_ERROR, "CODE FAILURE...FLOATING-POINT EXCEPTION (CORE DUMP)\n\n" );
    } break;
    case SIGILL: {
        logging( *_log_error_rank, CRITICAL_ERROR, "ILLEGAL INSTRUCTION (CORE DUMP)\n\n" );
    } break;
    case SIGBUS: {
        logging( *_log_error_rank, CRITICAL_ERROR, "BUS ERROR (CORE DUMP)\n\n" );
    } break;
    case SIGINT: {
        logging( *_log_error_rank, CRITICAL_ERROR, "TERMINAL INTERRUPT\n\n" );
    } break;
    case SIGTERM: {
        logging( *_log_error_rank, CRITICAL_ERROR, "TERMINATION SIGNAL\n\n" );
    } break;
    case SIGQUIT: {
        logging( *_log_error_rank, CRITICAL_ERROR, "TERMINAL QUIT (CORE DUMP)\n\n" );
    } break;
    case SIGABRT: {
        logging( *_log_error_rank, CRITICAL_ERROR, "ABORT SIGNAL (CORE DUMP)\n\n" );
    } break;
    case SIGHUP: {
        logging( *_log_error_rank, CRITICAL_ERROR, "HANGUP\n\n" );
    } break;
    case SIGCHLD: {
        logging( *_log_error_rank, CRITICAL_ERROR, "CHILD STATUS CHANGED\n\n" );
    } break;
    case SIGPIPE: {
        logging( *_log_error_rank, CRITICAL_ERROR, "BROKEN PIPE\n\n" );
    } break;
    }
    logging( *_log_error_rank, CRITICAL_ERROR, "exit(1)\n\n" );
    exit( 1 );
}

void logging( int _rank, log_level_t _level, const char *_fmt, ... ) {
    test_op( _rank, >=, 0 );
    struct timeval time;
    gettimeofday( &time, NULL );
    time.tv_sec = time.tv_sec - _t0.tv_sec;
    time.tv_usec = time.tv_usec - _t0.tv_usec;
    if ( time.tv_usec < 0 )
        time.tv_usec += 1000000, time.tv_sec--;

    char filename[256];
    snprintf( filename, sizeof( filename ), "./logs/rank%d.log", _rank );

    FILE *fp = fopen( filename, "a+" );
    test( fp );

    char lvl[16];
    switch ( _level ) {
    case INFO: {
        strncpy( lvl, "INFO", sizeof( lvl ) );
        lvl[5] = '\0';
    } break;
    case SEND: {
        strncpy( lvl, "SEND", sizeof( lvl ) );
        lvl[5] = '\0';
    } break;
    case RECV: {
        strncpy( lvl, "RECV", sizeof( lvl ) );
        lvl[5] = '\0';
    } break;
    case WARNING: {
        strncpy( lvl, "WARNING", sizeof( lvl ) );
        lvl[8] = '\0';
    } break;
    case ERROR: {
        strncpy( lvl, "ERROR", sizeof( lvl ) );
        lvl[6] = '\0';
    } break;
    case CRITICAL_ERROR: {
        strncpy( lvl, "CRITICAL ERROR", sizeof( lvl ) );
        lvl[15] = '\0';
    } break;
    }

    char prefix[256];
    snprintf( prefix, sizeof( prefix ), "[%ld.%ld] RANK %d | [%s] --- ", time.tv_sec, time.tv_usec, _rank, lvl );

    char content[2048];
    va_list args;
    va_start( args, _fmt );
    vsnprintf( content, sizeof( content ), _fmt, args );
    va_end( args );

    fprintf( fp, "%s%s\n", prefix, content );

    fclose( fp );
}

/* ------ OBSOLETO MAS PERMANECE POR MOTIVOS DE MEMORIA HISTÓRICA (EU DIRIA) ------*/
// //=========================================================================
// //===           BALANCEAMENTO DE CARGA | LEITURA E ENVIO                ===
// //=========================================================================
// struct sys_mem_stats sms;
// get_memory_stats( &sms );

// if ( ROOT ) {
//     logging( RANK, INFO, "Abrindo o arquivo para leitura" );

//     int open_ret = MPI_File_open( MPI_COMM_WORLD, input_path, MPI_MODE_RDONLY, MPI_INFO_NULL, &f_in );
//     test( open_ret == MPI_SUCCESS );

//     struct sys_mem_stats mem_info[SIZE];
//     for ( int i = 1; i < SIZE; i++ ) {
//         MPI_Recv( &mem_info[i], 1, MPI_MEMINFO_T, i, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE );
//         logging( RANK, INFO, "Rank %d possui %lu bytes livres", i, mem_info[i].free );
//     }
//     // Vou enviar os primeiros 10 Milhões de arestas para começar o processamento e diminuir o tempo ocioso
//     // Sem consultar memória
//     edges_to_process = malloc( sizeof( edge_t ) * 10000000 );
//     test( edges_to_process );

//     uint32_t readed = 0;
//     for ( size_t i = 0; i < RANK; ++i ) {
//         MPI_File_read( f_in, &edges_to_process[readed], 1000000, MPI_EDGE_T, MPI_STATUS_IGNORE );
//         logging( RANK, INFO, "Lendo arestas %lu ate %lu", readed, readed + 1000000 );
//         readed += 1000000;
//     }

// } else {
//     MPI_Send( &sms, 1, MPI_MEMINFO_T, 0, 0, MPI_COMM_WORLD );

//     edges_to_process = malloc( sizeof( edge_t ) * 10000000 );
//     test( edges_to_process );

//     MPI_Recv( edges_to_process, 10000000, MPI_EDGE_T, 0, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE );
//     my_edges = 10000000;
// }

// logging( RANK, INFO, "Leitura e distribuicao de dados concluida com sucesso." );

// // Captura o término da fase de Leitura e Ordenação
// gettimeofday( &t_io_sort, NULL );

// //=========================================================================
// //===                      ALOCAÇÃO DOS COMPONENTES                     ===
// //=========================================================================
// logging( RANK, INFO, "Verificando limites de memoria antes de alocar componentes" );
// int can_alloc_global = can_allocate( vertex_len, sizeof( component_t ) );
// int can_alloc_local = can_allocate( vertex_len, sizeof( component_t ) );
// int can_alloc_mst = ( ROOT ) ? can_allocate( vertex_len - 1, sizeof( edge_t ) ) : 1;

// int mem_ok = ( can_alloc_global >= (int)vertex_len && can_alloc_local >= (int)vertex_len && can_alloc_mst >= 0 );

// int global_mem_ok = 0;
// MPI_Allreduce( &mem_ok, &global_mem_ok, 1, MPI_INT, MPI_LAND, MPI_COMM_WORLD );
// logging( RANK, INFO, "Resultado da verificacao de memoria global: mem_ok=%d", global_mem_ok );

// if ( !global_mem_ok ) {
//     logging( RANK, ERROR, "Abortando devido a limite de memoria atingido em um ou mais ranks" );
//     if ( ROOT ) {
//         print_error( "Memoria insuficiente",
//                      "Um ou mais ranks nao possuem memoria livre suficiente para "
//                      "os componentes.",
//                      NULL, -1 );
//     }
//     free( edges_to_process );
//     MPI_Finalize();
//     return 1;
// }

// MPI_Barrier( MPI_COMM_WORLD );

// logging( RANK, INFO, "Alocando estruturas components_global e components_local de tamanho %u", vertex_len );
// component_t *components_local = malloc( vertex_len * sizeof( component_t ) );

// if ( !components_global || !components_local ) {
//     logging( RANK, ERROR, "Falha ao alocar componentes locais (global_ptr=%p, local_ptr=%p)", components_global,
//              components_local );
//     if ( ROOT ) {
//         print_error( "Falha ao alocar memoria", "Nao foi possivel alocar os vetores de componentes", NULL, -1 );
//     }
//     if ( components_global )
//         free( components_global );
//     if ( components_local )
//         free( components_local );
//     free( edges_to_process );
//     MPI_Finalize();
//     return 1;
// }

// // INICIALIZAÇÃO DOS COMPONENTES (CADA VÉRTICE É SEU PRÓPRIO COMPONENTE)
// for ( size_t i = 0; i < vertex_len; ++i ) {
//     components_local[i]._id = i;
//     RESET_EDGE( components_local[i].best_edge );
// }

// // ALOCAÇÃO DA LISTA DE ARESTAS DA MST (APENAS RANK 0 PRECISA ARMAZENAR)
// edge_t *mst_arestas = NULL;
// int mst_quantidade_arestas = 0;
// if ( ROOT ) {
//     logging( RANK, INFO, "Alocando vetor mst_arestas no Rank 0 com capacidade %u", vertex_len - 1 );
//     mst_arestas = malloc( ( vertex_len - 1 ) * sizeof( edge_t ) );
//     if ( !mst_arestas && vertex_len > 1 ) {
//         logging( RANK, ERROR, "Falha ao alocar mst_arestas no Rank 0" );
//         print_error( "Falha ao alocar memoria", "Nao foi possivel alocar as arestas da MST no Rank 0", NULL, -1 );
//         free( components_global );
//         free( components_local );
//         free( edges_to_process );
//         MPI_Finalize();
//         return 1;
//     }
// }

// bool finalizado = false;
// size_t quantidade_componentes = vertex_len;
// int iteracao = 0;

// // Inicializa barra de progresso no Rank 0 para verbosidade normal ou maior
// ProgressBar pb;
// if ( ROOT && verbosity >= 1 ) {
//     progress_bar_init( &pb, PROGRESS_STYLE_BLOCKS, TERMINAL_COLOR_GREEN );
//     pb.label = "Progresso do Processamento";
//     progress_bar_update( &pb, 0.0f );
//     fflush( stdout );
// }

// //=========================================================================
// //===                           LOOP PRINCIPAL                          ===
// //=========================================================================
// logging( RANK, INFO, "Iniciando processamento. Componentes iniciais: %zu", quantidade_componentes );
// while ( !finalizado ) {
//     iteracao++;
//     logging( RANK, INFO, "Iteracao %d: %zu componentes restantes", iteracao, quantidade_componentes );
//     if ( verbosity >= 2 && ROOT ) {
//         printf( "[VERBOSE] Iteracao %d: %zu componentes restantes\n", iteracao, quantidade_componentes );
//     }

//     // 2.1 - RESETAR O ESTADO DAS MELHORES ARESTAS DE CADA COMPONENTE LOCAL
//     for ( size_t i = 0; i < vertex_len; ++i ) {
//         RESET_EDGE( components_local[i].best_edge );
//     }

//     // 2.2 - BUSCA LOCAL (CADA PROCESSO PROCURA NAS SUAS ARESTAS DESIGNADAS)
//     uint64_t edges_evaluated = 0;
//     uint64_t cross_edges_found = 0;
//     for ( size_t i = 0; i < my_edges; ++i ) {
//         uint32_t u = edges_to_process[i].src;
//         uint32_t v = edges_to_process[i].dst;
//         double w = edges_to_process[i].w;

//         edges_evaluated++;
//         // Obtém as raízes atuais dos componentes
//         uint32_t raiz_u = find_component( u, components_local );
//         uint32_t raiz_v = find_component( v, components_local );

//         // Apenas consideramos arestas que conectam componentes diferentes
//         if ( raiz_u != raiz_v ) {
//             cross_edges_found++;
//             // Atualiza a melhor aresta para o componente de u
//             if ( components_local[raiz_u].best_edge.src == INF32 || components_local[raiz_u].best_edge.w > w ) {
//                 components_local[raiz_u].best_edge = edges_to_process[i];
//             }
//             // Atualiza a melhor aresta para o componente de v
//             if ( components_local[raiz_v].best_edge.src == INF32 || components_local[raiz_v].best_edge.w > w ) {
//                 components_local[raiz_v].best_edge = edges_to_process[i];
//             }
//         }
//     }
//     logging( RANK, INFO,
//              "Busca local concluida na iteracao %d: %lu arestas avaliadas, %lu "
//              "arestas de fronteira encontradas",
//              iteracao, edges_evaluated, cross_edges_found );

//     // SINCRONIZAÇÃO GLOBAL DE TODAS AS MELHORES ARESTAS ENCONTRADAS VIA REDUÇÃO
//     logging( RANK, INFO,
//              "Iniciando MPI_Allreduce para consolidar as melhores arestas "
//              "globalmente" );
//     MPI_Allreduce( components_local, components_global, vertex_len, MPI_COMPONENT_T, MPI_BEST_EDGE_OP,
//                    MPI_COMM_WORLD );

//     // 2.3 - SINCRONIZAÇÃO E FUSÃO (UNIÃO DOS COMPONENTES)
//     logging( RANK, INFO, "Iniciando fusao de componentes e merge" );
//     bool arestas_adicionadas_nesta_rodada = false;
//     uint32_t merges_performed = 0;

//     for ( size_t i = 0; i < vertex_len; ++i ) {
//         if ( components_global[i].best_edge.src != INF32 ) {
//             edge_t escolhida = components_global[i].best_edge;
//             uint32_t raiz_u = find_component( escolhida.src, components_local );
//             uint32_t raiz_v = find_component( escolhida.dst, components_local );

//             if ( raiz_u != raiz_v ) {
//                 merges_performed++;
//                 // Rank 0 armazena a aresta na MST final
//                 if ( ROOT ) {
//                     mst_arestas[mst_quantidade_arestas++] = escolhida;
//                 }

//                 // Merge dos componentes
//                 components_local[raiz_u]._id = raiz_v;
//                 quantidade_componentes--;
//                 arestas_adicionadas_nesta_rodada = true;
//             }
//         }
//     }
//     logging( RANK, INFO,
//              "Rodada de fusao concluida: %u fusoes realizadas, componentes "
//              "restantes: %zu",
//              merges_performed, quantidade_componentes );

//     // Atualiza barra de progresso no Rank 0 para verbosidade normal
//     if ( ROOT && verbosity >= 1 ) {
//         float prog = 0.0f;
//         if ( vertex_len > 1 ) {
//             prog = 1.0f - (float)( quantidade_componentes - 1 ) / ( vertex_len - 1 );
//         } else {
//             prog = 1.0f;
//         }
//         if ( prog < 0.0f )
//             prog = 0.0f;
//         if ( prog > 1.0f )
//             prog = 1.0f;
//         progress_bar_update( &pb, prog );
//         fflush( stdout );
//     }

//     // 2.4 - CHECAGEM DE PARADA
//     if ( !arestas_adicionadas_nesta_rodada || quantidade_componentes <= 1 ) {
//         logging( RANK, INFO,
//                  "Criterio de parada atendido: arestas_adicionadas=%d, "
//                  "componentes_restantes=%zu",
//                  arestas_adicionadas_nesta_rodada, quantidade_componentes );
//         finalizado = true;
//     }
// }

// // Garante que a barra de progresso termine com 100% e pule linha
// if ( ROOT && verbosity >= 1 ) {
//     if ( pb.value < 1.0f ) {
//         progress_bar_update( &pb, 1.0f );
//         fflush( stdout );
//     }
// }

// logging( RANK, INFO, "Loop concluido. Componentes finais: %zu, total de arestas na MST: %d", quantidade_componentes,
//          mst_quantidade_arestas );

// // Captura término do loop
// gettimeofday( &t_loop, NULL );

// //=========================================================================
// //===                     FINALIZAÇÃO E LIMPEZA                         ===
// //=========================================================================
// logging( RANK, INFO, "Iniciando finalizacao e desalocacao de recursos" );

// double peso_total = 0.0;
// if ( ROOT && mst_arestas ) {
//     for ( int i = 0; i < mst_quantidade_arestas; ++i ) {
//         peso_total += mst_arestas[i].w;
//     }
// }

// // Liberação das estruturas para capturar o tempo total até a desalocação
// if ( components_global )
//     free( components_global );
// if ( components_local )
//     free( components_local );
// if ( edges_to_process )
//     free( edges_to_process );
// MPI_Op_free( &MPI_BEST_EDGE_OP );
// MPI_Type_free( &MPI_EDGE_T );
// MPI_Type_free( &MPI_COMPONENT_T );

// // Captura o tempo total de execução final
// gettimeofday( &t_end, NULL );
// logging( RANK, INFO, "Recursos desalocados e tipos MPI liberados." );

// //=========================================================================
// //===                     APRESENTAÇÃO DE RESULTADOS                    ===
// //=========================================================================
// // Cálculo de tempos decorridos
// double time_lb = get_elapsed( t_start, t_meta_lb );
// double time_io = get_elapsed( t_meta_lb, t_io_sort );
// double time_process = get_elapsed( t_io_sort, t_loop );
// double time_total = get_elapsed( t_start, t_end );

// logging( RANK, INFO,
//          "Tempos de execucao: Setup=%f s, I/O/Ordenacao=%f s, "
//          "Processamento=%f s, Total=%f s",
//          time_lb, time_io, time_process, time_total );

// // Redução dos tempos para exibir mínimo e máximo entre os ranks
// double max_time_lb = 0.0, min_time_lb = 0.0;
// double max_time_io = 0.0, min_time_io = 0.0;
// double max_time_process = 0.0, min_time_process = 0.0;

// MPI_Reduce( &time_lb, &max_time_lb, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD );
// MPI_Reduce( &time_lb, &min_time_lb, 1, MPI_DOUBLE, MPI_MIN, 0, MPI_COMM_WORLD );

// MPI_Reduce( &time_io, &max_time_io, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD );
// MPI_Reduce( &time_io, &min_time_io, 1, MPI_DOUBLE, MPI_MIN, 0, MPI_COMM_WORLD );

// MPI_Reduce( &time_process, &max_time_process, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD );
// MPI_Reduce( &time_process, &min_time_process, 1, MPI_DOUBLE, MPI_MIN, 0, MPI_COMM_WORLD );

// // Apresentação de saídas dependente da verbosidade no terminal do Rank 0
// if ( ROOT ) {
//     if ( verbosity >= 1 ) {
//         print_box_double( "ARVORE GERADORA MINIMA (MST) ENCONTRADA", TERMINAL_COLOR_GREEN );

//         char info_text[512];
//         snprintf( info_text, sizeof( info_text ), "QUANTIDADE DE ARESTAS NA MST: %d  |  PESO TOTAL DA MST: %.9f",
//                   mst_quantidade_arestas, peso_total );
//         print_box( info_text, TERMINAL_COLOR_CYAN );

//         // Tabela de arestas (se o grafo for pequeno)
//         if ( vertex_len < 100 && mst_arestas ) {
//             TableStyle ts;
//             table_init( &ts, 3, TERMINAL_COLOR_CYAN, TERMINAL_COLOR_WHITE, TERMINAL_COLOR_GREEN );
//             table_set_col_width( &ts, 0, 15 );
//             table_set_col_width( &ts, 1, 15 );
//             table_set_col_width( &ts, 2, 15 );

//             const char *headers[] = { "Origem (src)", "Peso (w)", "Destino (dst)" };
//             table_print_header( &ts, headers );

//             for ( int i = 0; i < mst_quantidade_arestas; ++i ) {
//                 char src_buf[32], w_buf[32], dst_buf[32];
//                 snprintf( src_buf, sizeof( src_buf ), "%u", mst_arestas[i].src );
//                 snprintf( w_buf, sizeof( w_buf ), "%.2f", mst_arestas[i].w );
//                 snprintf( dst_buf, sizeof( dst_buf ), "%u", mst_arestas[i].dst );
//                 const char *row[] = { src_buf, w_buf, dst_buf };
//                 table_print_row( &ts, row );
//             }
//             table_print_footer( &ts );

//             // Exibe o caminho da árvore
//             printf( "Caminho da arvore:\n  " );
//             for ( int i = 0; i < mst_quantidade_arestas; ++i ) {
//                 if ( i > 0 ) {
//                     printf( " %s\u2192%s ", TERMINAL_COLOR_YELLOW, TERMINAL_COLOR_RESET );
//                 }
//                 printf( "%u %s(%.2f)%s %u", mst_arestas[i].src, TERMINAL_COLOR_GREEN, mst_arestas[i].w,
//                         TERMINAL_COLOR_RESET, mst_arestas[i].dst );
//             }
//             printf( "\n\n" );
//         }

//         // Tabela de tempos de execução
//         TableStyle ts_time;
//         table_init( &ts_time, 4, TERMINAL_COLOR_BLUE, TERMINAL_COLOR_WHITE, TERMINAL_COLOR_CYAN );
//         table_set_col_width( &ts_time, 0, 30 );
//         table_set_col_width( &ts_time, 1, 15 );
//         table_set_col_width( &ts_time, 2, 15 );
//         table_set_col_width( &ts_time, 3, 15 );

//         const char *time_headers[] = { "Etapa do Algoritmo", "Rank 0 (s)", "Min Ranks (s)", "Max Ranks (s)" };
//         table_print_header( &ts_time, time_headers );

//         char r0_buf[32], min_buf[32], max_buf[32];

//         snprintf( r0_buf, sizeof( r0_buf ), "%.6f", time_lb );
//         snprintf( min_buf, sizeof( min_buf ), "%.6f", min_time_lb );
//         snprintf( max_buf, sizeof( max_buf ), "%.6f", max_time_lb );
//         const char *row_lb[] = { "1. Setup / Balanc. Carga", r0_buf, min_buf, max_buf };
//         table_print_row( &ts_time, row_lb );

//         snprintf( r0_buf, sizeof( r0_buf ), "%.6f", time_io );
//         snprintf( min_buf, sizeof( min_buf ), "%.6f", min_time_io );
//         snprintf( max_buf, sizeof( max_buf ), "%.6f", max_time_io );
//         const char *row_io[] = { "2. Leitura E/S & Ordenacao", r0_buf, min_buf, max_buf };
//         table_print_row( &ts_time, row_io );

//         snprintf( r0_buf, sizeof( r0_buf ), "%.6f", time_process );
//         snprintf( min_buf, sizeof( min_buf ), "%.6f", min_time_process );
//         snprintf( max_buf, sizeof( max_buf ), "%.6f", max_time_process );
//         const char *row_proc[] = { "3. Processamento MST", r0_buf, min_buf, max_buf };
//         table_print_row( &ts_time, row_proc );

//         snprintf( r0_buf, sizeof( r0_buf ), "%.6f", time_total );
//         const char *row_total[] = { "Total Geral Executado", r0_buf, "-", "-" };
//         table_print_row( &ts_time, row_total );

//         table_print_footer( &ts_time );

//         print_success( NULL, "Arvore Geradora Minima (MST) calculada com sucesso!" );
//     } else {
//         // verbosity == 0 (quiet mode) - only print final MST details in a single
//         // plain line to stdout
//         printf( "MST: arestas=%d, peso=%.9f, tempo=%.6fs\n", mst_quantidade_arestas, peso_total, time_total );
//     }

//     if ( mst_arestas )
//         free( mst_arestas );
// }
