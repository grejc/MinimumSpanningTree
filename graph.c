#include "graph.h"

#ifndef USING_MPI
// DSU ========================================================================

dsu_t *dsu_create(int32_t n) {
  dsu_t *dsu = (dsu_t *)malloc(sizeof(dsu_t));
  if (!dsu)
    return NULL;

  dsu->n = n;
  dsu->parent = (int32_t *)malloc(n * sizeof(int32_t));
  dsu->rank = (int32_t *)malloc(n * sizeof(int32_t));

  if (!dsu->parent || !dsu->rank) {
    free(dsu->parent);
    free(dsu->rank);
    free(dsu);
    return NULL;
  }

  for (int32_t i = 0; i < n; i++) {
    dsu->parent[i] = i;
    dsu->rank[i] = 0;
  }

  return dsu;
}

void dsu_free(dsu_t *dsu) {
  if (dsu) {
    free(dsu->parent);
    free(dsu->rank);
    free(dsu);
  }
}

int32_t dsu_find(dsu_t *dsu, int32_t i) {
  if (dsu->parent[i] == i)
    return i;
  return dsu->parent[i] = dsu_find(dsu, dsu->parent[i]);
}

void dsu_unite(dsu_t *dsu, int32_t i, int32_t j) {
  int32_t root_i = dsu_find(dsu, i);
  int32_t root_j = dsu_find(dsu, j);

  if (root_i != root_j) {
    if (dsu->rank[root_i] < dsu->rank[root_j]) {
      dsu->parent[root_i] = root_j;
    } else if (dsu->rank[root_i] > dsu->rank[root_j]) {
      dsu->parent[root_j] = root_i;
    } else {
      dsu->parent[root_i] = root_j;
      dsu->rank[root_j]++;
    }
  }
}

#else

void setup_index_components_vector(uint32_t **_V, uint32_t _n){
    if ( !_V || !(*_V) ) return;

    for (size_t i = 0; i < _n; ++i){
        (*_V)[i] = i;
    }
    
}

#endif