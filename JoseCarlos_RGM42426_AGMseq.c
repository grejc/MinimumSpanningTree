#include <float.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>

#include "utils.h"

#define INF32 0xFFFFFFFF
#define _10M 10000000
#define _800M 800000000
#define RESET_EDGE( e ) ( e.src = INF32, e.dst = INF32, e.w = DBL_MAX )

typedef struct edge_s {
    uint32_t src;
    uint32_t dst;
    double w;
} edge_t;

typedef struct component_s {
    uint32_t _id;
    edge_t best_edge;
} component_t;

uint32_t find_component( uint32_t vertex_id, component_t *components );

int main( int argc, char **argv ) {
    struct timeval initial_time, final_time, algorithm_start, algorithm_end;
    gettimeofday( &initial_time, NULL );

    ArgParser ap;
    argp_init( &ap, "minspatree_seq", "1.1.0", "Minimum Spanning Tree with sequential algorithm." );
    argp_add_flag( &ap, 'd', "debug", "Debug Mode" );
    argp_add_option( &ap, 'o', "output", "FILE", "Output file for the MST", "./out_seq.txt" );
    argp_add_pos( &ap, "input", "Input file with dataset (binary)", 1 );

    if ( !argp_parse( &ap, argc, argv ) ) {
        argp_print_error( &ap );
        return 1;
    }

    const char *output_path = argp_get( &ap, "output" );
    const char *input_path = argp_pos( &ap, "input" );
    int _debug = argp_flag( &ap, "debug" );

    uint32_t vertex_len = _10M;
    uint32_t edges_len = _800M;
    uint32_t chunk_size = _10M;

    gettimeofday( &algorithm_start, NULL );

    edge_t *E_line = malloc( sizeof( edge_t ) * vertex_len );
    test( E_line != NULL );
    uint32_t m = 0;

    edge_t *new_E_line = malloc( sizeof( edge_t ) * vertex_len );
    test( new_E_line != NULL );

    component_t *components = malloc( sizeof( component_t ) * vertex_len );
    test( components != NULL );

    edge_t *chunk = malloc( sizeof( edge_t ) * chunk_size );
    test( chunk != NULL );

    FILE *fp = fopen( input_path, "rb" );
    if ( !fp ) {
        printf( "Error opening input file: %s\n", input_path );
        return 1;
    }

    uint32_t processed_edges = 0;
    int lote_idx = 1;

    while ( processed_edges < edges_len ) {
        uint32_t to_read = edges_len - processed_edges;
        if ( to_read > chunk_size ) {
            to_read = chunk_size;
        }

        size_t read_items = fread( chunk, sizeof( edge_t ), to_read, fp );
        if ( read_items != to_read ) {
            printf( "Error reading chunk from input file. Expected %u, got %zu\n", to_read, read_items );
            break;
        }
        processed_edges += to_read;

        if ( _debug ) {
            printf( "Processando lote %d (%u arestas)...\n", lote_idx++, to_read );
        }

        for ( uint32_t i = 0; i < vertex_len; ++i ) {
            components[i]._id = i;
            RESET_EDGE( components[i].best_edge );
        }
        uint32_t components_len = vertex_len;
        uint32_t new_m = 0;
        bool done = false;

        while ( !done ) {
            for ( uint32_t i = 0; i < vertex_len; ++i ) {
                RESET_EDGE( components[i].best_edge );
            }

            for ( uint32_t i = 0; i < m; ++i ) {
                uint32_t u = E_line[i].src;
                uint32_t v = E_line[i].dst;
                double w = E_line[i].w;

                uint32_t root_u = find_component( u, components );
                uint32_t root_v = find_component( v, components );

                if ( root_u != root_v ) {
                    if ( components[root_u].best_edge.src == INF32 || components[root_u].best_edge.w > w ) {
                        components[root_u].best_edge = E_line[i];
                    }
                    if ( components[root_v].best_edge.src == INF32 || components[root_v].best_edge.w > w ) {
                        components[root_v].best_edge = E_line[i];
                    }
                }
            }

            for ( uint32_t i = 0; i < to_read; ++i ) {
                uint32_t u = chunk[i].src;
                uint32_t v = chunk[i].dst;
                double w = chunk[i].w;

                uint32_t root_u = find_component( u, components );
                uint32_t root_v = find_component( v, components );

                if ( root_u != root_v ) {
                    if ( components[root_u].best_edge.src == INF32 || components[root_u].best_edge.w > w ) {
                        components[root_u].best_edge = chunk[i];
                    }
                    if ( components[root_v].best_edge.src == INF32 || components[root_v].best_edge.w > w ) {
                        components[root_v].best_edge = chunk[i];
                    }
                }
            }

            bool edges_added = false;
            for ( uint32_t i = 0; i < vertex_len; ++i ) {
                if ( components[i].best_edge.src != INF32 ) {
                    edge_t chosen = components[i].best_edge;
                    uint32_t root_u = find_component( chosen.src, components );
                    uint32_t root_v = find_component( chosen.dst, components );

                    if ( root_u != root_v ) {
                        new_E_line[new_m++] = chosen;
                        components[root_u]._id = root_v;
                        components_len--;
                        edges_added = true;
                    }
                }
            }

            if ( !edges_added || components_len == 1 ) {
                done = true;
            }
        }

        memcpy( E_line, new_E_line, sizeof( edge_t ) * new_m );
        m = new_m;
    }

    fclose( fp );
    gettimeofday( &algorithm_end, NULL );

    FILE *fp_out = fopen( output_path, "w" );
    if ( fp_out ) {
        double final_weight = 0.0;
        for ( size_t i = 0; i < m; ++i ) {
            fprintf( fp_out, "%u %.12f %u\n", E_line[i].src, E_line[i].w, E_line[i].dst );
            final_weight += E_line[i].w;
        }
        printf( "FINAL WEIGHT -> %.12f\n", final_weight );
        fclose( fp_out );
    } else {
        printf( "Error opening output file: %s\n", output_path );
    }

    printf( "TIME: %ldµs\n", ( algorithm_end.tv_sec - algorithm_start.tv_sec ) * 1000000 +
                                 ( algorithm_end.tv_usec - algorithm_start.tv_usec ) );

    free( E_line );
    free( new_E_line );
    free( chunk );
    free( components );
    argp_free( &ap );

    return 0;
}

uint32_t find_component( uint32_t vertex_id, component_t *components ) {
    if ( components[vertex_id]._id == vertex_id ) {
        return vertex_id;
    }
    components[vertex_id]._id = find_component( components[vertex_id]._id, components );
    return components[vertex_id]._id;
}
