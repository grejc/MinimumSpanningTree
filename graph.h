#ifndef GRAPH_H
#define GRAPH_H

#include <stdint.h>
#include <stdlib.h>
#include <inttypes.h>
#include <stdint.h>
#include <math.h>

#ifdef GREJC_SETUP_MPI_DEFAULT
#define USING_MPI
#include <mpi/mpi.h>
#elif GREJC_SETUP_MPI_FALLBACK
#define USING_MPI
#include <mpi.h>
#endif

#include "utils.h"

#ifndef USING_MPI

/// @brief Structure representing a weighted edge in a non-directional graph.
/// 
/// @param src Source vertex ID.
/// @param dest Destination vertex ID.
/// @param weight Weight of the edge.
typedef struct edge_s {
  int32_t src;
  int32_t dest;
  int64_t weight;
} edge_t;

typedef struct component_s {
    unsigned int _id;
    edge_t *edges;
    unsigned int n_edges;
} component_t;

/// ===========================================================================
/// Structure representing a Disjoint Set Union (DSU).
/// ---
/// @param parent Array where each element points to its parent in the set.
/// @param rank Array used for union by rank/size optimization.
/// @param n Total number of elements in the DSU.
/// ===========================================================================
typedef struct dsu_s {
  int32_t *parent;
  int32_t *rank;
  int32_t n;
} dsu_t;

// DSU ========================================================================

/// ===========================================================================
/// Creates and initializes a new Disjoint Set Union (DSU) structure.
/// ---
/// @param n The number of elements (vertices) to be managed by the DSU.
/// ---
/// Example:
/// ```c
/// dsu_t *dsu = dsu_create(100);
/// ```
/// Expected data:
/// A pointer to the initialized dsu_t structure, where each element is its own
/// parent.
/// ===========================================================================
dsu_t *dsu_create(int32_t n);

/// ===========================================================================
/// Frees the memory allocated for a DSU structure.
/// ---
/// @param dsu Pointer to the DSU structure to be freed.
/// ---
/// Example:
/// ```c
/// dsu_free(dsu);
/// ```
/// Expected data:
/// No return value. The memory associated with the DSU is deallocated.
/// ===========================================================================
void dsu_free(dsu_t *dsu);

/// ===========================================================================
/// Finds the representative (root) of the set containing element i.
/// Implements path compression for efficiency.
/// ---
/// @param dsu Pointer to the DSU structure.
/// @param i The element to find the root for.
/// ---
/// Example:
/// ```c
/// int32_t root = dsu_find(dsu, 5);
/// ```
/// Expected data:
/// The ID of the representative element of the set containing i.
/// ===========================================================================
int32_t dsu_find(dsu_t *dsu, int32_t i);

/// ===========================================================================
/// Unites the sets containing elements i and j.
/// Implements union by rank to keep the tree shallow.
/// ---
/// @param dsu Pointer to the DSU structure.
/// @param i First element.
/// @param j Second element.
/// ---
/// Example:
/// ```c
/// dsu_unite(dsu, 1, 2);
/// ```
/// Expected data:
/// No return value. The sets containing i and j are merged into one.
/// ===========================================================================
void dsu_unite(dsu_t *dsu, int32_t i, int32_t j);

#else /* USING_MPI */

typedef struct edge_s {
  uint32_t src;
  uint32_t dst;
  double w;
} edge_t;

void setup_index_components_vector(uint32_t **_V, uint32_t _n);

#endif /* USING_MPI */

#endif /* GRAPH_H */
