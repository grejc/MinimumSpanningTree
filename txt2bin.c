#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <limits.h>

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <input.txt> <output_prefix>\n", argv[0]);
        fprintf(stderr, "  Produces: <output_prefix>.meta.txt and <output_prefix>.bin\n");
        return 1;
    }
    const char *input_path = argv[1];
    const char *prefix = argv[2];

    char meta_path[1024], bin_path[1024];
    snprintf(meta_path, sizeof(meta_path), "%s.meta.txt", prefix);
    snprintf(bin_path, sizeof(bin_path), "%s.bin", prefix);

    FILE *fin = fopen(input_path, "r");
    if (!fin) { perror("fopen input"); return 1; }

    int32_t n_vertices, n_edges;
    if (fscanf(fin, "%d\n%d", &n_vertices, &n_edges) != 2) {
        fprintf(stderr, "Error reading vertex/edge count\n");
        fclose(fin);
        return 1;
    }
    printf("Vertices: %d, Edges: %d\n", n_vertices, n_edges);

    int64_t *weights = malloc(n_edges * sizeof(int64_t));
    int32_t *srcs = malloc(n_edges * sizeof(int32_t));
    int32_t *dests = malloc(n_edges * sizeof(int32_t));
    if (!weights || !srcs || !dests) {
        fprintf(stderr, "Memory allocation failed\n");
        return 1;
    }

    int64_t w_min = INT64_MAX, w_max = INT64_MIN;
    for (int i = 0; i < n_edges; i++) {
        if (fscanf(fin, "%d %d %ld", &srcs[i], &dests[i], &weights[i]) != 3) {
            fprintf(stderr, "Error reading edge %d\n", i);
            fclose(fin);
            return 1;
        }
        if (weights[i] < w_min) w_min = weights[i];
        if (weights[i] > w_max) w_max = weights[i];
    }
    fclose(fin);

    printf("Weight range: [%ld, %ld]\n", (long)w_min, (long)w_max);

    // Write metadata .txt
    FILE *fmeta = fopen(meta_path, "w");
    if (!fmeta) { perror("fopen meta"); return 1; }
    fprintf(fmeta, "%d\n%d\n", n_vertices, n_edges);
    fclose(fmeta);
    printf("Metadata written to %s\n", meta_path);

    // Write binary .bin (int32 src, int32 dst, double weight)
    FILE *fbin = fopen(bin_path, "wb");
    if (!fbin) { perror("fopen bin"); return 1; }

    double range = (double)(w_max - w_min);
    for (int i = 0; i < n_edges; i++) {
        int32_t src = srcs[i];
        int32_t dst = dests[i];
        double w_norm = (range > 0.0) ? (double)(weights[i] - w_min) / range : 0.0;
        fwrite(&src, sizeof(int32_t), 1, fbin);
        fwrite(&dst, sizeof(int32_t), 1, fbin);
        fwrite(&w_norm, sizeof(double), 1, fbin);
    }
    fclose(fbin);
    printf("Binary written to %s (%ld edges, %ld bytes)\n",
           bin_path, (long)n_edges, (long)n_edges * 16L);

    free(srcs);
    free(dests);
    free(weights);
    return 0;
}
