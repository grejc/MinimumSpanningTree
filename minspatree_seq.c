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

#include "graph.h"
#include "utils.h"

static char *buffer;
static size_t buffer_pos = 0;
static size_t buffer_size = 0;

edge_t *mst(int n_vertices, int n_edges, edge_t *edges);
int find_connected_components(int n_vertices, component_t **components);
int get_merged_id(int *parent, int i);

void fast_io_init(const char *filename);
int next_int();
int64_t boruvka(int32_t n, int32_t m, edge_t *edges, const char *output_path);

int main(int argc, char **argv) {
  struct timeval initial_time, final_time, algorithm_start, algorithm_end;
  // Parsing arguments
  // ----------------------------------------------------------------------------------------------
  ArgParser ap;

  argp_init(&ap, "minspatree_seq", "1.1.0",
            "Minimum Spanning Tree with sequential Otakar Boruvka algorithm (and an adaptation).");
  argp_add_flag(&ap, 'd', "debug", "Debug Mode");
  argp_add_flag(&ap, 'b', "berryallen", "Berry Allen Mode (fast as fuck)");
  argp_add_option(&ap, 'o', "output", "FILE", "Output file for the MST",
                  "./out_seq.txt");
  argp_add_pos(&ap, "input", "Input file with dataset", 1);

  if (!argp_parse(&ap, argc, argv)) {
    argp_print_error(&ap);
    return 1;
  }

  int _berryallen = argp_flag(&ap, "berryallen");
  const char *output_path = argp_get(&ap, "output");
  const char *input_path = argp_pos(&ap, "input");

  if (_berryallen) {
    struct timeval si, ei, sp, ep;
    gettimeofday(&si, NULL);
    fast_io_init(input_path);
    int n = next_int(), m = next_int();
    edge_t *E = malloc(m * sizeof(edge_t));
    for (int i = 0; i < m; ++i)
      E[i].src = next_int(), E[i].dest = next_int(), E[i].weight = next_int();
    gettimeofday(&ei, NULL), gettimeofday(&sp, NULL);
    int final_weight = boruvka(n, m, E, NULL);
    gettimeofday(&ep, NULL);

    printf("Final weight: %d\nRead time: %ldµs\nAlgorithm time: %ldµs\n",
           final_weight,
           (ei.tv_sec - si.tv_sec) * 1000000 + (ei.tv_usec - si.tv_usec),
           (ep.tv_sec - sp.tv_sec) * 1000000 + (ep.tv_usec - sp.tv_usec));
    free(E);
    return 0;
  }
  int _debug = argp_flag(&ap, "debug");

  // ----------------------------------------------------------------------------------------------------------------
  TERMINAL_CLEAN_SCREEN();

  print_box_double("Minimum Spanning Tree", TERMINAL_COLOR_CYAN);

  FILE *fp = fopen(input_path, "r");
  test(fp);

  if (_debug)
    print_success(NULL, "File opened successfully");

  int32_t n_vertices, n_edges;
  size_t result =fscanf(fp, "%d\n%d", &n_vertices, &n_edges);
  test(result == 2);
  test(n_vertices > 0 && n_edges > 0);

  if (_debug) {
    char buf[100];
    sprintf(buf, "Number of vertices: %d", n_vertices);
    print_success(NULL, buf);
    sprintf(buf, "Number of edges: %d", n_edges);
    print_success(NULL, buf);
  }

  edge_t *edges = malloc(n_edges * sizeof(edge_t));
  test(edges);

  ProgressBar pb_read;
  progress_bar_init(&pb_read, PROGRESS_STYLE_SIMPLE, TERMINAL_COLOR_BLUE);
  pb_read.label = "Reading edges";
  gettimeofday(&initial_time, NULL);

  for (int i = 0; i < n_edges; i++) {
    size_t result = fscanf(fp, "%d %d %ld", &edges[i].src, &edges[i].dest, &edges[i].weight);
    test(result == 3);

    if (n_edges >= 100 && i % (n_edges / 100) == 0)
      progress_bar_update(&pb_read, (float)i / n_edges);
  }
  progress_bar_update(&pb_read, 1.0f);
  gettimeofday(&final_time, NULL);
  fclose(fp);

  if (_debug) {
    print_success(NULL, "Edges read successfully");
    char buf[100];
    sprintf(buf, "Time to read edges: %ldµs",
            (final_time.tv_sec - initial_time.tv_sec) * 1000000 +
                (final_time.tv_usec - initial_time.tv_usec));
    print_success(NULL, buf);
  }

  //=================================================================================================================

  gettimeofday(&algorithm_start, NULL);
  edge_t *F = mst(n_vertices, n_edges, edges);
  gettimeofday(&algorithm_end, NULL);
  //=================================================================================================================

  int64_t total_weight = 0;
  for (int i = 0; i < n_vertices - 1 && F[i].src != -1; i++) {
    total_weight += F[i].weight;
    if (_debug) {
      // printf("%d %d %ld\n", F[i].src, F[i].dest, F[i].weight);
      char buf[100];
      sprintf(buf, "Total weight of MST: %lu", total_weight);
      print_success(NULL, buf);
    }
  }

  TableStyle ts;
  table_init(&ts, 2, TERMINAL_COLOR_CYAN, TERMINAL_COLOR_WHITE,
             TERMINAL_COLOR_BLUE);
  const char *h[] = {"Metric", "Value"};
  table_print_header(&ts, h);
  char buf[100];
  sprintf(buf, "%ldus",
          (final_time.tv_sec - initial_time.tv_sec) * 1000000 +
              (final_time.tv_usec - initial_time.tv_usec));
  const char *r[] = {"Time to read edges", buf};
  table_print_row(&ts, r);
  sprintf(buf, "%ldus",
          (algorithm_end.tv_sec - algorithm_start.tv_sec) * 1000000 +
              (algorithm_end.tv_usec - algorithm_start.tv_usec));
  const char *m[] = {"Time to compute MST", buf};
  table_print_row(&ts, m);
  sprintf(buf, "%lu", total_weight);
  const char *w[] = {"Total weight of MST", buf};
  table_print_row(&ts, w);

  table_print_footer(&ts);

  if (output_path != NULL) {
    FILE *fp = fopen(output_path, "w");
    test(fp != NULL);

    ProgressBar pb_write_output_file;
    progress_bar_init(&pb_write_output_file, PROGRESS_STYLE_SIMPLE, TERMINAL_COLOR_GREEN);
    pb_write_output_file.label = "Writing output";

    fprintf(fp, "%d\n%d\n", n_vertices, n_edges);
    for (int i = 0; i < n_vertices - 1 && F[i].src != -1; i++) {
      fprintf(fp, "%d %d %ld\n", F[i].src, F[i].dest, F[i].weight);

      if (n_edges >= 100 && i % (n_edges / 100) == 0)
          progress_bar_update(&pb_write_output_file, (float)i / n_edges);
    }
    fclose(fp);
    progress_bar_update(&pb_write_output_file, 1.0f);
  }

  free(edges);
  argp_free(&ap);
}

void fast_io_init(const char *filename) {
  FILE *fp = fopen(filename, "rb");
  test(fp != NULL);

  struct stat st;
  if (stat(filename, &st) != 0) {
    print_error("Stat failed", filename, NULL, 1);
  }
  buffer_size = st.st_size;

  buffer = malloc(buffer_size + 1);
  test(buffer != NULL);

  size_t read_size = fread(buffer, 1, buffer_size, fp);
  test(read_size == buffer_size);
  buffer[buffer_size] = '\0';

  fclose(fp);
}

int next_int() {
  while (buffer[buffer_pos] &&
         (buffer[buffer_pos] < '0' || buffer[buffer_pos] > '9') &&
         buffer[buffer_pos] != '-') {
    buffer_pos++;
  }

  if (!buffer[buffer_pos])
    return -1;

  int sign = 1;
  if (buffer[buffer_pos] == '-') {
    sign = -1;
    buffer_pos++;
  }

  int res = 0;
  while (buffer[buffer_pos] >= '0' && buffer[buffer_pos] <= '9') {
    res = res * 10 + (buffer[buffer_pos] - '0');
    buffer_pos++;
  }
  return res * sign;
}

int64_t boruvka(int32_t n, int32_t m, edge_t *edges, const char *output_path) {
  dsu_t *dsu = dsu_create(n);
  // test(dsu != NULL);

  int32_t num_components = n;
  int64_t mst_weight = 0;
  edge_t *mst_edges = NULL;
  if (n > 1) {
    mst_edges = malloc(sizeof(edge_t) * (n - 1));
    // test(mst_edges != NULL);
  }
  int32_t mst_edge_count = 0;

  int32_t *cheapest = NULL;
  if (n > 0) {
    cheapest = malloc(sizeof(int32_t) * n);
    // test(cheapest != NULL);
  }

  while (num_components > 1) {
    for (int32_t i = 0; i < n; i++)
      cheapest[i] = -1;

    int32_t edges_found = 0;
    for (int32_t i = 0; i < m; i++) {
      int32_t set1 = dsu_find(dsu, edges[i].src);
      int32_t set2 = dsu_find(dsu, edges[i].dest);

      if (set1 != set2) {
        edges_found = 1;
        if (cheapest[set1] == -1 ||
            edges[i].weight < edges[cheapest[set1]].weight) {
          cheapest[set1] = i;
        }
        if (cheapest[set2] == -1 ||
            edges[i].weight < edges[cheapest[set2]].weight) {
          cheapest[set2] = i;
        }
      }
    }

    if (!edges_found)
      break;

    int added_this_iter = 0;
    for (int32_t i = 0; i < n; i++) {
      if (cheapest[i] != -1) {
        int32_t set1 = dsu_find(dsu, edges[cheapest[i]].src);
        int32_t set2 = dsu_find(dsu, edges[cheapest[i]].dest);

        if (set1 != set2) {
          mst_weight += edges[cheapest[i]].weight;
          if (mst_edges) {
            mst_edges[mst_edge_count++] = edges[cheapest[i]];
          }
          dsu_unite(dsu, set1, set2);
          num_components--;
          added_this_iter++;
        }
      }
    }
    if (added_this_iter == 0)
      break;
  }

  // printf("MST Total Weight: %" PRId64 "\n", mst_weight);
  if (output_path != NULL) {
    for (int32_t i = 0; i < mst_edge_count; i++) {
      printf("%d --(%" PRId64 ")--> %d\n", mst_edges[i].src,
             mst_edges[i].weight, mst_edges[i].dest);
    }
  }

  if (mst_edges)
    free(mst_edges);
  if (cheapest)
    free(cheapest);
  dsu_free(dsu);

  return mst_weight;
}

int find_connected_components(int n_vertices, component_t **components_ptr) {
  if ((*components_ptr) == NULL) {
    return -1;
  }
  component_t *components = *components_ptr;
  int *old_to_new = malloc(n_vertices * sizeof(int));
  test(old_to_new != NULL);
  for (int i = 0; i < n_vertices; i++)
    old_to_new[i] = -1;

  int n_components = 0;
  for (int i = 0; i < n_vertices; i++) {
    int old_id = components[i]._id;
    if (old_to_new[old_id] == -1)
      old_to_new[old_id] = n_components++;
    components[i]._id = old_to_new[old_id];
  }

  free(old_to_new);
  return n_components;
}

component_t *find_component_of_vertex(component_t *components, int vertex) {
  return &components[vertex];
}

bool preferable_over(edge_t *new_edge, edge_t *current) {
  if (current->src == -1)
    return true;
  if (new_edge->weight < current->weight)
    return true;
  if (new_edge->weight == current->weight) {
    int ns = new_edge->src < new_edge->dest ? new_edge->src : new_edge->dest;
    int nd = new_edge->src < new_edge->dest ? new_edge->dest : new_edge->src;
    int cs = current->src < current->dest ? current->src : current->dest;
    int cd = current->src < current->dest ? current->dest : current->src;
    return ns < cs || (ns == cs && nd < cd);
  }
  return false;
}

int get_merged_id(int *parent, int i) {
  while (parent[i] != i) {
    parent[i] = parent[parent[i]];
    i = parent[i];
  }
  return i;
}

edge_t *mst(int n_vertices, int n_edges, edge_t *edges) {
  edge_t *F = malloc(
      (n_vertices - 1) *
      sizeof(edge_t)); // By definition the maximum of n-1 edges in the MST
  test(F != NULL);

  component_t *components = malloc(n_vertices * sizeof(component_t));
  test(components != NULL);

  for (unsigned int i = 0; i < n_vertices; i++) {
    components[i]._id = i;
    components[i].edges = NULL;
    components[i].n_edges = 0;
  }

  int F_size = 0;
  bool done = false;
  while (!done) {
    int n_components = find_connected_components(n_vertices, &components);
    test(n_components != -1);

    if (n_components == 1) {
      done = true;
      break;
    }

    edge_t *best_edges = malloc(n_components * sizeof(edge_t));
    test(best_edges != NULL);
    for (int i = 0; i < n_components; ++i) {
      best_edges[i].src = best_edges[i].dest = -1;
      best_edges[i].weight = 0; // The weight is not relevant for the initial
                                // value since the src and dest are -1
    }

    for (edge_t *it = edges; it < edges + n_edges; ++it) {
      component_t *comp_src = find_component_of_vertex(components, it->src);
      component_t *comp_dst = find_component_of_vertex(components, it->dest);

      if (comp_src->_id != comp_dst->_id) {
        if (preferable_over(it, &best_edges[comp_src->_id])) {
          best_edges[comp_src->_id] = *it;
        }
        if (preferable_over(it, &best_edges[comp_dst->_id])) {
          best_edges[comp_dst->_id] = *it;
        }
      }
    }

    bool added_edge = false;
    int *parent_id = malloc(n_components * sizeof(int));
    for (int i = 0; i < n_components; i++) {
      parent_id[i] = i;
    }
    for (int i = 0; i < n_components; i++) {
      if (best_edges[i].src != -1) {
        int comp_u =
            get_merged_id(parent_id, components[best_edges[i].src]._id);
        int comp_v =
            get_merged_id(parent_id, components[best_edges[i].dest]._id);

        if (comp_u != comp_v) {
          F[F_size++] = best_edges[i];
          added_edge = true;

          parent_id[comp_v] = comp_u;
        }
      }
    }

    if (added_edge) {
      for (int j = 0; j < n_vertices; j++) {
        components[j]._id = get_merged_id(parent_id, components[j]._id);
      }
    }

    free(parent_id);
    free(best_edges);

    if (!added_edge) {
      done = true;
    }
  }

  free(components);
  return F;
}
