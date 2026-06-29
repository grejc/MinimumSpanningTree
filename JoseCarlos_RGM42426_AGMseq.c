// Minha máquina
// Iterações: 12
// TIME: 954839145µs |
// FINAL WEIGHT -> 75117.848736822314
//
// Máquina da facul
// TIME:
// FINAL WEIGHT ->

#include <float.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/cdefs.h>
#include <sys/stat.h>
#include <sys/sysinfo.h>
#include <sys/time.h>

#include "utils.h"

#define INF32 0xFFFFFFFF
#define _10M 10000000
#define _800M 800000000

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

uint32_t find_component( uint32_t vertex_id, component_t *components );
void get_memory_stats( struct sys_mem_stats *_sms );
uint64_t can_allocate( size_t _len, size_t _size );
static int compare_edges( const void *a, const void *b );
bool preferable_over( const edge_t *new_edge, const edge_t *current );

int main( int argc, char **argv ) {
    bool _debug = true;
    char input_path[256] = "/home/null/college/ProgParalela/Test/graph.bin";

    if ( argc == 1 ) {
        strcpy( input_path, "/home/local/rgm42426/graph.bin" );
    }

    if ( _debug )
        print_success( NULL, "Input Path: %s", input_path );

    struct timeval algorithm_start, algorithm_end;

    const uint32_t vertex_len = _10M;
    const uint32_t edges_len = _800M;

    gettimeofday( &algorithm_start, NULL );

    // 1. Alocações globais
    edge_t *mst = malloc( sizeof( edge_t ) * vertex_len );
    test( mst != NULL );
    if ( _debug )
        print_success( NULL, "Memory allocated to MST array" );

    component_t *components = malloc( sizeof( component_t ) * vertex_len );
    test( components != NULL );
    if ( _debug )
        print_success( NULL, "Memory allocated to components" );

    uint64_t chunk_size = can_allocate( edges_len, sizeof( edge_t ) );
    chunk_size = !chunk_size ? _10M: chunk_size; // Fallback caso não consigo ler o arquivo de memória
    if ( _debug )
        print_success( NULL, "Chunk size defined to: %lu", chunk_size );

    edge_t *chunk = malloc( sizeof( edge_t ) * chunk_size );
    test( chunk != NULL );
    if ( _debug )
        print_success( NULL, "Memory allocated to Chunk Buffer" );

    FILE *fp = fopen( input_path, "rb" );
    test_op( fp, !=, NULL );
    if ( _debug )
        print_success( NULL, "File %s opened for reading", input_path );

    // 2. Inicialização dos componentes
    for ( size_t i = 0; i < vertex_len; ++i ) {
        components[i]._id = i;
        components[i].best_edge.src = INF32; // INF32 atua como sentinela (nulo)
    }

    uint32_t num_components = vertex_len;
    uint32_t mst_size = 0;
    bool done = false;
    int pass = 1;

    // 3. Algoritmo
    while ( !done && num_components > 1 ) {
        if ( _debug )
            printf( "\n--- Starting Pass %d (Components left: %u) ---\n", pass++, num_components );

        // Reseta as melhores arestas para a nova passagem
        for ( size_t i = 0; i < vertex_len; ++i ) {
            components[i].best_edge.src = INF32;
        }

        // Volta o ponteiro do arquivo para o início a cada passagem
        fseek( fp, 0, SEEK_SET );
        uint32_t processed_edges = 0;

        // Lendo e processando o arquivo em Chunks
        while ( processed_edges < edges_len ) {
            uint32_t to_read = edges_len - processed_edges;
            if ( to_read > chunk_size )
                to_read = chunk_size;

            size_t read_items = fread( chunk, sizeof( edge_t ), to_read, fp );
            test_op( read_items, ==, to_read, "Error reading chunk." );
            processed_edges += to_read;

            // Encontra a melhor aresta global para cada componente
            for ( size_t i = 0; i < to_read; ++i ) {
                const uint32_t u = find_component( chunk[i].src, components );
                const uint32_t v = find_component( chunk[i].dst, components );

                if ( u != v ) {
                    if ( preferable_over( &chunk[i], &components[u].best_edge ) ) {
                        components[u].best_edge = chunk[i];
                    }
                    if ( preferable_over( &chunk[i], &components[v].best_edge ) ) {
                        components[v].best_edge = chunk[i];
                    }
                }
            }
        } // Fim da leitura do arquivo (Fim da passagem)

        // 4. Contração (Union) e adição na Árvore Geradora Mínima
        bool edges_added = false;

        for ( size_t i = 0; i < vertex_len; ++i ) {
            // Se o componente raiz encontrou uma aresta válida
            if ( components[i]._id == i && components[i].best_edge.src != INF32 ) {
                edge_t best = components[i].best_edge;

                uint32_t root_u = find_component( best.src, components );
                uint32_t root_v = find_component( best.dst, components );

                if ( root_u != root_v ) {
                    components[root_u]._id = root_v; // Union
                    mst[mst_size++] = best;          // Salva na MST
                    num_components--;
                    edges_added = true;
                }
            }
        }

        // Se passamos por todo o grafo e nenhuma aresta uniu componentes, finalizamos.
        if ( !edges_added ) {
            done = true;
        }
    }

    fclose( fp );
    gettimeofday( &algorithm_end, NULL );

    printf( "TIME: %ldµs\n", ( algorithm_end.tv_sec - algorithm_start.tv_sec ) * 1000000 +
        ( algorithm_end.tv_usec - algorithm_start.tv_usec ) );

    // 5. Finalização e Escrita do Output
    qsort( mst, mst_size, sizeof( edge_t ), compare_edges );

    FILE *fp_out = fopen( "./output_sequencial", "w" );
    if ( fp_out ) {
        double final_weight = 0.0;
        for ( size_t i = 0; i < mst_size; ++i ) {
            fprintf( fp_out, "%u %.12f %u\n", mst[i].src, mst[i].w, mst[i].dst );
            final_weight += mst[i].w;
        }
        printf( "FINAL WEIGHT -> %.12f\n", final_weight );
        fclose( fp_out );
    }

    // 6. Limpeza total (Sem Memory Leaks)
    free( mst );
    free( components );
    free( chunk );
    if ( input_path )
        free( input_path );

    return 0;
}

// ---------------- FUNÇÕES AUXILIARES ---------------- //

static int compare_edges( const void *a, const void *b ) {
    const edge_t *A = (edge_t *)a;
    const edge_t *B = (edge_t *)b;
    if ( A->w < B->w ) return -1;
    if ( A->w > B->w ) return  1;
    return 0;
}

/**
    * @brief Algoritmo Find Iterativo (Two-Pass Path Compression)
    * Evita Stack Overflow em grafos muito profundos.
    */
uint32_t find_component( uint32_t vertex_id, component_t *components ) {
    uint32_t root = vertex_id;

    // Passo 1: Encontrar a raiz absoluta
    while ( components[root]._id != root ) {
        root = components[root]._id;
    }

    // Passo 2: Compressão de caminho
    uint32_t curr = vertex_id;
    while ( curr != root ) {
        uint32_t next = components[curr]._id;
        components[curr]._id = root;
        curr = next;
    }

    return root;
}

/**
    * @brief Resolve empates de forma determinística
    */
bool preferable_over( const edge_t *new_edge, const edge_t *current ) {
    // Se não há aresta atual salva, a nova automaticamente vence
    if ( current->src == INF32 )
        return true;
    if ( new_edge->src == INF32 )
        return false;

    if ( new_edge->w < current->w )
        return true;

    // Desempate por ID para evitar ciclos paralelos
    if ( new_edge->w == current->w ) {
        const uint32_t ns = new_edge->src < new_edge->dst ? new_edge->src : new_edge->dst;
        const uint32_t nd = new_edge->src < new_edge->dst ? new_edge->dst : new_edge->src;
        const uint32_t cs = current->src < current->dst ? current->src : current->dst;
        const uint32_t cd = current->src < current->dst ? current->dst : current->src;
        return ns < cs || ( ns == cs && nd < cd );
    }
    return false;
}

uint64_t can_allocate( size_t _len, size_t _size ) {
    if ( _len == 0 || _size == 0 )
        return 0; // INVALID ARGUEMNTS

    struct sys_mem_stats sms;
    get_memory_stats( &sms );

    const uint64_t MT = sms.total;
    const uint64_t MU = sms.used;
    const uint64_t MD = sms.free;

    const uint64_t T = MT - (MT + 4) / 5; // 80% da memória total
    const uint64_t C = _size * _len; // Custo de alocação de todos os itens
    const uint64_t n = T > MU ? ( T - MU ) / _size : 0; // O que pode ser usado é maior que já foi utilizado?

    return n < _len ? n: _len;
}

void get_memory_stats( struct sys_mem_stats *_sms ) {
    if ( !_sms )
        return;

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
            if ( found == 2 )
                break;
        }
        fclose( fp );
        _sms->used = _sms->total - _sms->free;

        return;
    }

    // ERROR GETTING DATA
    _sms->free = _sms->used = _sms->total = 0;
}
