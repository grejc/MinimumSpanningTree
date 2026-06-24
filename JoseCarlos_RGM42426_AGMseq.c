#include <inttypes.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>

#include "utils.h"

/// @brief Structure representing a weighted edge in a non-directional graph.
///
/// @param src  Source vertex ID.
/// @param dest Destination vertex ID.
/// @param w    Weight of the edge.
typedef struct edge_s {
    int32_t src;    ///< vertex number id
    int32_t dst;    ///< vertex number id
    int64_t weight; ///< edge weight
} edge_t;

/// @brief Structure representing a component
///
/// @param _id          Component identifier
/// @param best_edge    The lower weight edge of component
typedef struct component_s {
    unsigned int _id;
    edge_t *best_edge;
} component_t;

bool preferable_over( edge_t *new_edge, edge_t *current );
int find_component( int vertex_id, component_t *components );

int main( int argc, char **argv ) {
    struct timeval initial_time, final_time, algorithm_start, algorithm_end;
    // Parsing arguments
    // ----------------------------------------------------------------------------------------------
    ArgParser ap;

    argp_init( &ap, "minspatree_seq", "1.1.0", "Minimum Spanning Tree with sequential algorithm." );
    argp_add_flag( &ap, 'd', "debug", "Debug Mode" );
    argp_add_option( &ap, 'o', "output", "FILE", "Output file for the MST", "./out_seq.txt" );
    argp_add_pos( &ap, "input", "Input file with dataset", 1 );

    if ( !argp_parse( &ap, argc, argv ) ) {
        argp_print_error( &ap );
        return 1;
    }

    const char *output_path = argp_get( &ap, "output" );
    const char *input_path = argp_pos( &ap, "input" );

    int _debug = argp_flag( &ap, "debug" );

    // Read ---------------------------------------------------------
    FILE *fp = fopen( input_path, "r" );
    test( fp );

    int32_t n_vertices, n_edges;
    size_t result = fscanf( fp, "%d\n%d", &n_vertices, &n_edges );
    test( result == 2 );
    test( n_vertices > 0 && n_edges > 0 );

    edge_t *edges = malloc( n_edges * sizeof( edge_t ) );
    test( edges );

    for ( int i = 0; i < n_edges; i++ ) {
        size_t result = fscanf( fp, "%d %d %ld", &edges[i].src, &edges[i].dst, &edges[i].weight );
        test( result == 3 );
    }
    gettimeofday( &final_time, NULL );
    fclose( fp );

    // Process ------------------------------------------------------
    gettimeofday( &algorithm_start, NULL );

    // --- E' = {}
    edge_t *E_line = malloc( ( n_vertices - 1 ) * sizeof( edge_t ) ); // Máximo de arestas na MST
    test( E_line != NULL );
    int mst_edge_count = 0; // Contador de arestas adicionadas

    // --- Components Vector
    component_t *components = malloc( n_vertices * sizeof( component_t ) );
    test( components != NULL );

    // Inicialização dos componentes: cada vértice é seu próprio componente
    // Isso deve ser feito fora do loop while
    for ( int i = 0; i < n_vertices; i++ ) {
        components[i]._id = i;
        components[i].best_edge = NULL;
    }

    size_t components_len = n_vertices;
    bool done = false;

    // Aresta sentinela para inicialização de comparações
    edge_t sentinel_edge = { -1, -1, -1 };

    while ( !done ) {
        // 1. Identificar componentes conexos é implícito pela estrutura Union-Find
        // (components) Basta verificar o tamanho.

        if ( components_len == 1 ) {
            done = true;
            break;
        }

        // Vetor temporário para armazenar a melhor aresta de cada componente
        // (indexado pelo ID do componente raiz) Alocamos dinamicamente para evitar
        // estourar a pilha se n_vertices for grande
        edge_t **best_edges_buffer = malloc( n_vertices * sizeof( edge_t * ) );
        test( best_edges_buffer != NULL );

        // Inicializar com a sentinela
        for ( int i = 0; i < n_vertices; i++ ) {
            best_edges_buffer[i] = &sentinel_edge;
        }

        // 2. Encontrar a menor aresta que sai de cada componente
        for ( size_t i = 0; i < n_edges; ++i ) {
            int root_u = find_component( edges[i].src, components );
            int root_v = find_component( edges[i].dst, components );

            // Só nos interessam arestas que ligam componentes DIFERENTES
            if ( root_u != root_v ) {
                // Verifica se é a melhor aresta para o componente de u
                if ( preferable_over( &edges[i], best_edges_buffer[root_u] ) ) {
                    best_edges_buffer[root_u] = &edges[i];
                }
                // Verifica se é a melhor aresta para o componente de v
                if ( preferable_over( &edges[i], best_edges_buffer[root_v] ) ) {
                    best_edges_buffer[root_v] = &edges[i];
                }
            }
        }

        // 3. Adicionar as melhores arestas encontradas à floresta
        bool edges_added_this_round = false;

        // Iteramos sobre os possíveis IDs de componentes
        for ( unsigned int i = 0; i < n_vertices; i++ ) {
            // Verificamos se 'i' é realmente uma raiz de um componente válido
            // e se encontrou alguma aresta para ele (não é sentinela)
            if ( components[i]._id == i && best_edges_buffer[i] != &sentinel_edge ) {

                edge_t *chosen_edge = best_edges_buffer[i];

                // Recalculamos as raízes pois elas podem ter mudado nesta iteração
                // se já processamos outra aresta que uniu componentes.
                int root_u = find_component( chosen_edge->src, components );
                int root_v = find_component( chosen_edge->dst, components );

                if ( root_u != root_v ) {
                    // Adiciona a aresta à MST
                    E_line[mst_edge_count++] = *chosen_edge;

                    // Merge dos componentes (Union)
                    // Fazemos root_u apontar para root_v (ou vice-versa, a escolha é
                    // arbitrária para MST)
                    components[root_u]._id = root_v;

                    components_len--;
                    edges_added_this_round = true;
                }
            }
        }

        free( best_edges_buffer );

        // Se não adicionou nenhuma aresta e ainda há mais de 1 componente, o grafo
        // é desconexo
        if ( !edges_added_this_round ) {
            done = true;
        }
    }

    gettimeofday( &algorithm_end, NULL );

    // Output -------------------------------------------------------
    FILE *fp_out = fopen( output_path, "w" );
    int64_t final_weight = 0;
    fprintf( fp_out, "%u\n%u\n", n_vertices, mst_edge_count );
    for ( size_t i = 0; i < mst_edge_count; ++i ) {
        fprintf( fp_out, "%u %u %ld\n", E_line[i].src, E_line[i].dst, E_line[i].weight );
        final_weight += E_line[i].weight;
    }
    printf( "FINAL WEIGHT → %ld\n", final_weight );
    printf( "TIME: %ldµs\n", ( algorithm_end.tv_sec - algorithm_start.tv_sec ) * 1000000 +
                                 ( algorithm_end.tv_usec - algorithm_start.tv_usec ) );

    // Free ---------------------------------------------------------
    free( E_line );
    free( edges );
    free( components );
    argp_free( &ap );

    return 0;
}

int find_component( int vertex_id, component_t *components ) {
    if ( components[vertex_id]._id == (unsigned int)vertex_id ) {
        return vertex_id;
    }
    // Compressão de caminho
    components[vertex_id]._id = find_component( components[vertex_id]._id, components );
    return components[vertex_id]._id;
}

bool preferable_over( edge_t *new_edge, edge_t *current ) {
    // Tratamento para sentinela (current->src == -1)
    if ( current->src == -1 )
        return true;

    if ( new_edge->weight < current->weight )
        return true;

    if ( new_edge->weight == current->weight ) {
        // Critério de desempate: menor origem, ou mesma origem e menor destino
        // A normalização (src < dst) garante consistência para grafos não
        // direcionados
        int ns = new_edge->src < new_edge->dst ? new_edge->src : new_edge->dst;
        int nd = new_edge->src < new_edge->dst ? new_edge->dst : new_edge->src;
        int cs = current->src < current->dst ? current->src : current->dst;
        int cd = current->src < current->dst ? current->dst : current->src;

        return ns < cs || ( ns == cs && nd < cd );
    }
    return false;
}
