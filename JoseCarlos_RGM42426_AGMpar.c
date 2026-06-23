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

#include <sys/sysinfo.h>
#include <sys/time.h>
#include <sys/stat.h>

#include "utils.h"

#define INF32 0xFFFFFFFF
#define RESET_EDGE(e) (e.src = INF32, e.dst = INF32, e.w = DBL_MAX)

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

int compare_edges(const void *a, const void *b);
void get_memory_stats(struct sys_mem_stats *_sms);
void reduce_best_edge_operation(void *invec, void *inoutvec, int *len,
                                MPI_Datatype *datatype);
uint32_t find_component(uint32_t vertex_id, component_t *components);
void critical_error_logger(int _signal);
void logging(int _rank, log_level_t _level, const char *_fmt, ...);
int can_allocate(size_t _len, size_t _size);

//-----------------------------------------------------------------------------

void __startup__(void) __attribute__((constructor));

void __startup__() { gettimeofday(&_t0, NULL); }

static inline double get_elapsed(struct timeval t0, struct timeval t1) {
  return (t1.tv_sec - t0.tv_sec) + (t1.tv_usec - t0.tv_usec) / 1e6;
}

int main(int argc, char **argv) {
  // Capture start time
  struct timeval t_start, t_meta_lb, t_io_sort, t_loop, t_end;
  gettimeofday(&t_start, NULL);

  //=========================================================================
  //===                      INICIALIZANDO MPI                            ===
  //=========================================================================
  MPI_Init(&argc, &argv);
  int RANK, SIZE;

  _RANK = &RANK, _SIZE = &SIZE;

  MPI_Comm_rank(MPI_COMM_WORLD, &RANK), MPI_Comm_size(MPI_COMM_WORLD, &SIZE);

  // Intercepta --help ou --version antes do parser para evitar que ranks extras travem
  bool help_requested = false;
  bool version_requested = false;
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
      help_requested = true;
    }
    if (strcmp(argv[i], "--version") == 0) {
      version_requested = true;
    }
  }

  if (help_requested || version_requested) {
    if (RANK == 0) {
      ArgParser ap;
      argp_init(&ap, "JoseCarlos_RGM42426_AGMpar", "1.1.0",
                "Algoritmo Paralelo (MPI) para calcular a Arvore Geradora Minima (MST)");
      argp_add_option(&ap, 'v', "verbose", "LEVEL", "Nivel de verbosidade: 0 (quiet), 1 (normal), 2 (debug/verbose)", "1");
      argp_add_pos(&ap, "input", "Caminho do arquivo binario de entrada", 1);
      
      if (version_requested) {
        printf("JoseCarlos_RGM42426_AGMpar 1.1.0\n");
      } else {
        argp_usage(&ap);
      }
      argp_free(&ap);
    }
    MPI_Finalize();
    return 0;
  }

  // DEFINIÇÃO DE TIPOS CUSTOMIZADOS MPI
  MPI_Datatype MPI_EDGE_T;
  int MPI_EDGE_T_blocklen[] = {1, 1, 1};
  MPI_Datatype MPI_EDGE_T_types[] = {MPI_UINT32_T, MPI_UINT32_T, MPI_DOUBLE};
  MPI_Aint MPI_EDGE_T_offsets[] = {offsetof(edge_t, src), offsetof(edge_t, dst),
                                   offsetof(edge_t, w)};
  MPI_Type_create_struct(3, MPI_EDGE_T_blocklen, MPI_EDGE_T_offsets,
                         MPI_EDGE_T_types, &MPI_EDGE_T);
  MPI_Type_commit(&MPI_EDGE_T);

  MPI_Datatype MPI_COMPONENT_T;
  int MPI_COMPONENT_T_blocklen[] = {1, 1};
  MPI_Datatype MPI_COMPONENT_T_types[] = {MPI_UINT32_T, MPI_EDGE_T};
  MPI_Aint MPI_COMPONENT_T_offsets[] = {offsetof(component_t, _id),
                                        offsetof(component_t, best_edge)};
  MPI_Type_create_struct(2, MPI_COMPONENT_T_blocklen, MPI_COMPONENT_T_offsets,
                         MPI_COMPONENT_T_types, &MPI_COMPONENT_T);
  MPI_Type_commit(&MPI_COMPONENT_T);

  // DEFINIÇÃO DE OPERAÇÃO CUSTOMIZADAS
  MPI_Op MPI_BEST_EDGE_OP;
  MPI_Op_create(reduce_best_edge_operation, true, &MPI_BEST_EDGE_OP);

  //=========================================================================
  //===                   INICIALIZANDO LOGGERS                            ===
  //=========================================================================
  _log_error_rank = &RANK;

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

  // Cria diretório de logs se necessário
  if (RANK == 0) {
    mkdir("./logs", 0777);
  }
  MPI_Barrier(MPI_COMM_WORLD);

  //=========================================================================
  //===                PARSING DE PARÂMETROS COM ARGPARSER                ===
  //=========================================================================
  ArgParser ap;
  argp_init(&ap, "JoseCarlos_RGM42426_AGMpar", "1.1.0",
            "Algoritmo Paralelo (MPI) para calcular a Arvore Geradora Minima (MST)");
  argp_add_option(&ap, 'v', "verbose", "LEVEL", "Nivel de verbosidade: 0 (quiet), 1 (normal), 2 (debug/verbose)", "1");
  argp_add_pos(&ap, "input", "Caminho do arquivo binario de entrada", 1);

  int parse_ok = argp_parse(&ap, argc, argv);
  if (!parse_ok) {
    if (RANK == 0) {
      argp_print_error(&ap);
    }
    argp_free(&ap);
    MPI_Finalize();
    return 1;
  }

  const char *bin_path = argp_pos(&ap, "input");
  int verbosity = 1;
  const char *v_str = argp_get(&ap, "verbose");
  if (v_str) {
    verbosity = atoi(v_str);
  }
  argp_free(&ap);

  logging(RANK, INFO, "Processo inicializado no comunicador MPI com rank %d de %d", RANK, SIZE);
  logging(RANK, INFO, "Parametros do Parser: input='%s', verbosity=%d", bin_path, verbosity);

  if (RANK == 0 && verbosity >= 1) {
    print_box_double("ALGORITMO PARALELO (MPI)", TERMINAL_COLOR_BLUE);
  }

  //=========================================================================
  //===                     LEITURA DE METADADOS                          ===
  //=========================================================================
  uint32_t vertex_len = 0, edges_len = 0, my_edges = 0;
  edge_t *edges_to_process = NULL;

  logging(RANK, INFO, "Iniciando leitura de metadados");
  if (RANK == 0) {
    char meta_path[1024];
    size_t len = strlen(bin_path);
    if (len > 4 && strcmp(bin_path + len - 4, ".bin") == 0) {
      strncpy(meta_path, bin_path, len - 4);
      meta_path[len - 4] = '\0';
      strcat(meta_path, ".meta.txt");
    } else {
      snprintf(meta_path, sizeof(meta_path), "%s.meta.txt", bin_path);
    }

    logging(RANK, INFO, "Caminho do metadado calculado: %s", meta_path);
    FILE *fp = fopen(meta_path, "r");
    if (!fp) {
      logging(RANK, ERROR, "Erro ao abrir arquivo de metadados: %s", meta_path);
      print_error("Erro de Arquivo", "Rank 0: Erro ao abrir arquivo de metadados", NULL, -1);
    } else {
      if (fscanf(fp, "%u\n%u", &vertex_len, &edges_len) != 2) {
        logging(RANK, ERROR, "Erro ao ler formato de metadados em %s", meta_path);
        print_error("Erro de Formatacao", "Rank 0: Erro ao ler metadados", NULL, -1);
        vertex_len = 0;
        edges_len = 0;
      } else {
        logging(RANK, INFO, "Metadados carregados: %u vertices, %u arestas", vertex_len, edges_len);
      }
      fclose(fp);
    }
  }

  // Compartilha o número de vértices e arestas com todos os ranks
  MPI_Bcast(&vertex_len, 1, MPI_UINT32_T, 0, MPI_COMM_WORLD);
  MPI_Bcast(&edges_len, 1, MPI_UINT32_T, 0, MPI_COMM_WORLD);

  if (vertex_len == 0 || edges_len == 0) {
    logging(RANK, ERROR, "Abortando execucao devido a erro nos metadados (vertices: %u, arestas: %u)", vertex_len, edges_len);
    if (RANK == 0) {
      print_error("Dados Invalidos", "Numero de vertices ou arestas invalido (0)", NULL, -1);
    }
    MPI_Finalize();
    return 1;
  }

  //=========================================================================
  //===                    BALANCEAMENTO DE CARGA                         ===
  //=========================================================================
  logging(RANK, INFO, "Iniciando balanceamento de carga baseado na capacidade de memoria");
  struct sys_mem_stats sms;
  get_memory_stats(&sms);
  uint64_t local_capacity = (sms.total * 0.8 > sms.used) ? (sms.total * 0.8 - sms.used) : 0;
  logging(RANK, INFO, "Memoria do sistema local: Total=%lu, Usado=%lu, Disponivel=%lu, CapacidadeCalculada=%lu",
          sms.total, sms.used, sms.free, local_capacity);

  // Compartilha a capacidade de memória disponível de cada nó
  uint64_t *all_capacities = NULL;
  if (RANK == 0) {
    all_capacities = malloc(SIZE * sizeof(uint64_t));
  }
  MPI_Gather(&local_capacity, 1, MPI_UINT64_T, all_capacities, 1, MPI_UINT64_T, 0, MPI_COMM_WORLD);

  uint32_t start_idx = 0;
  uint32_t *send_counts = NULL;
  uint32_t *displs = NULL;
  if (RANK == 0) {
    send_counts = malloc(SIZE * sizeof(uint32_t));
    displs = malloc(SIZE * sizeof(uint32_t));

    uint64_t total_capacity = 0;
    for (int i = 0; i < SIZE; ++i) {
      total_capacity += all_capacities[i];
    }
    logging(RANK, INFO, "Capacidade de memoria total agregada de todos os ranks: %lu", total_capacity);

    if (total_capacity == 0) {
      logging(RANK, WARNING, "Capacidade de memoria total e 0. Usando divisao estatica uniforme.");
      // Divisão estática padrão se ninguém puder estimar
      uint32_t base = edges_len / SIZE;
      uint32_t rem = edges_len % SIZE;
      uint32_t current_displ = 0;
      for (int i = 0; i < SIZE; ++i) {
        send_counts[i] = base + ((uint32_t)i < rem ? 1 : 0);
        displs[i] = current_displ;
        current_displ += send_counts[i];
      }
    } else {
      // Distribuição proporcional ao limite de memória livre
      uint32_t edges_assigned = 0;
      uint32_t current_displ = 0;
      for (int i = 0; i < SIZE; ++i) {
        double ratio = (double)all_capacities[i] / total_capacity;
        send_counts[i] = (uint32_t)(ratio * edges_len);
        edges_assigned += send_counts[i];
      }
      // Ajuste de resíduos devido ao truncamento
      uint32_t rem_edges = edges_len - edges_assigned;
      for (uint32_t i = 0; i < rem_edges; ++i) {
        send_counts[i % SIZE]++;
      }
      for (int i = 0; i < SIZE; ++i) {
        displs[i] = current_displ;
        current_displ += send_counts[i];
      }
    }
    free(all_capacities);
  }

  // Distribui a carga calculada
  MPI_Scatter(send_counts, 1, MPI_UINT32_T, &my_edges, 1, MPI_UINT32_T, 0, MPI_COMM_WORLD);
  MPI_Scatter(displs, 1, MPI_UINT32_T, &start_idx, 1, MPI_UINT32_T, 0, MPI_COMM_WORLD);

  if (RANK == 0) {
    free(send_counts);
    free(displs);
  }

  logging(RANK, INFO, "Carga de trabalho atribuida: %u arestas (offset %u)", my_edges, start_idx);
  
  // Captura o término da fase de Setup e Load Balancing
  gettimeofday(&t_meta_lb, NULL);

  //=========================================================================
  //===                  LEITURA DE ARQUIVO & ORDENAÇÃO                    ===
  //=========================================================================
  logging(RANK, INFO, "Abrindo arquivo binario para leitura paralela via MPI-IO: %s", bin_path);
  MPI_File fh;
  int mpi_err = MPI_File_open(MPI_COMM_WORLD, bin_path, MPI_MODE_RDONLY,
                              MPI_INFO_NULL, &fh);
  if (mpi_err != MPI_SUCCESS) {
    logging(RANK, ERROR, "Falha ao abrir arquivo binario: %s", bin_path);
    if (RANK == 0) {
      print_error("Erro de MPI_File_open", "Nao foi possivel abrir o arquivo binario", NULL, -1);
    }
    MPI_Finalize();
    return 1;
  }

  edges_to_process = malloc(sizeof(edge_t) * my_edges);
  if (!edges_to_process) {
    logging(RANK, ERROR, "Falha na alocacao de memoria para as arestas locais (%u arestas)", my_edges);
    if (RANK == 0) {
      print_error("Erro de Alocacao", "Falha na alocacao de memoria para as arestas locais", NULL, -1);
    }
    MPI_File_close(&fh);
    MPI_Finalize();
    return 1;
  }

  MPI_Offset file_offset = (MPI_Offset)start_idx * sizeof(edge_t);
  MPI_Status read_status;

  logging(RANK, INFO, "Iniciando MPI_File_read_at_all com offset %lld para %u arestas", (long long)file_offset, my_edges);
  mpi_err = MPI_File_read_at_all(fh, file_offset, edges_to_process, my_edges,
                                 MPI_EDGE_T, &read_status);
  if (mpi_err != MPI_SUCCESS) {
    logging(RANK, ERROR, "Falha ao ler o arquivo usando MPI_File_read_at_all");
    if (RANK == 0) {
      print_error("Erro de MPI_File_read", "Falha ao ler o arquivo usando MPI_File_read_at_all", NULL, -1);
    }
    free(edges_to_process);
    MPI_File_close(&fh);
    MPI_Finalize();
    return 1;
  }

  MPI_File_close(&fh);
  logging(RANK, INFO, "Leitura paralela concluida e arquivo fechado com sucesso.");

  logging(RANK, INFO, "Ordenando as arestas locais (%u itens) usando qsort", my_edges);
  if (edges_to_process != NULL) {
    qsort(edges_to_process, my_edges, sizeof(edge_t), compare_edges);
    logging(RANK, INFO, "Ordenacao concluida com sucesso.");

    // Se a verbosidade for 2 (Debug) e o grafo for pequeno, exibe as arestas atribuídas ao processo no terminal
    if (verbosity >= 2 && edges_len < 100) {
      for (size_t i = 0; i < my_edges; ++i) {
        printf("Rank %d (aresta %zu): %u ← %.2f → %u\n", RANK, i,
               edges_to_process[i].src, edges_to_process[i].w,
               edges_to_process[i].dst);
      }
    }
  }

  // Captura o término da fase de Leitura e Ordenação
  gettimeofday(&t_io_sort, NULL);

  //=========================================================================
  //===                      ALOCAÇÃO DOS COMPONENTES                     ===
  //=========================================================================
  logging(RANK, INFO, "Verificando limites de memoria antes de alocar componentes");
  int can_alloc_global = can_allocate(vertex_len, sizeof(component_t));
  int can_alloc_local = can_allocate(vertex_len, sizeof(component_t));
  int can_alloc_mst = (RANK == 0) ? can_allocate(vertex_len - 1, sizeof(edge_t)) : 1;

  int mem_ok = (can_alloc_global >= (int)vertex_len &&
                can_alloc_local >= (int)vertex_len &&
                can_alloc_mst >= 0);

  int global_mem_ok = 0;
  MPI_Allreduce(&mem_ok, &global_mem_ok, 1, MPI_INT, MPI_LAND, MPI_COMM_WORLD);
  logging(RANK, INFO, "Resultado da verificacao de memoria global: mem_ok=%d", global_mem_ok);

  if (!global_mem_ok) {
    logging(RANK, ERROR, "Abortando devido a limite de memoria atingido em um ou mais ranks");
    if (RANK == 0) {
      print_error("Memoria insuficiente", "Um ou mais ranks nao possuem memoria livre suficiente para os componentes.", NULL, -1);
    }
    free(edges_to_process);
    MPI_Finalize();
    return 1;
  }

  MPI_Barrier(MPI_COMM_WORLD);

  logging(RANK, INFO, "Alocando estruturas components_global e components_local de tamanho %u", vertex_len);
  component_t *components_global = malloc(vertex_len * sizeof(component_t));
  component_t *components_local = malloc(vertex_len * sizeof(component_t));

  if (!components_global || !components_local) {
    logging(RANK, ERROR, "Falha ao alocar componentes locais (global_ptr=%p, local_ptr=%p)", components_global, components_local);
    if (RANK == 0) {
      print_error("Falha ao alocar memoria", "Nao foi possivel alocar os vetores de componentes", NULL, -1);
    }
    if (components_global) free(components_global);
    if (components_local) free(components_local);
    free(edges_to_process);
    MPI_Finalize();
    return 1;
  }

  // INICIALIZAÇÃO DOS COMPONENTES (CADA VÉRTICE É SEU PRÓPRIO COMPONENTE)
  for (size_t i = 0; i < vertex_len; ++i) {
    components_local[i]._id = i;
    RESET_EDGE(components_local[i].best_edge);
  }

  // ALOCAÇÃO DA LISTA DE ARESTAS DA MST (APENAS RANK 0 PRECISA ARMAZENAR)
  edge_t *mst_arestas = NULL;
  int mst_quantidade_arestas = 0;
  if (RANK == 0) {
    logging(RANK, INFO, "Alocando vetor mst_arestas no Rank 0 com capacidade %u", vertex_len - 1);
    mst_arestas = malloc((vertex_len - 1) * sizeof(edge_t));
    if (!mst_arestas && vertex_len > 1) {
      logging(RANK, ERROR, "Falha ao alocar mst_arestas no Rank 0");
      print_error("Falha ao alocar memoria", "Nao foi possivel alocar as arestas da MST no Rank 0", NULL, -1);
      free(components_global);
      free(components_local);
      free(edges_to_process);
      MPI_Finalize();
      return 1;
    }
  }

  bool finalizado = false;
  size_t quantidade_componentes = vertex_len;
  int iteracao = 0;

  // Inicializa barra de progresso no Rank 0 para verbosidade normal ou maior
  ProgressBar pb;
  if (RANK == 0 && verbosity >= 1) {
    progress_bar_init(&pb, PROGRESS_STYLE_BLOCKS, TERMINAL_COLOR_GREEN);
    pb.label = "Progresso do Processamento";
    progress_bar_update(&pb, 0.0f);
    fflush(stdout);
  }

  //=========================================================================
  //===                           LOOP PRINCIPAL                          ===
  //=========================================================================
  logging(RANK, INFO, "Iniciando processamento. Componentes iniciais: %zu", quantidade_componentes);
  while (!finalizado) {
    iteracao++;
    logging(RANK, INFO, "Iteracao %d: %zu componentes restantes", iteracao, quantidade_componentes);
    if (verbosity >= 2 && RANK == 0) {
      printf("[VERBOSE] Iteracao %d: %zu componentes restantes\n", iteracao, quantidade_componentes);
    }

    // 2.1 - RESETAR O ESTADO DAS MELHORES ARESTAS DE CADA COMPONENTE LOCAL
    for (size_t i = 0; i < vertex_len; ++i) {
      RESET_EDGE(components_local[i].best_edge);
    }

    // 2.2 - BUSCA LOCAL (CADA PROCESSO PROCURA NAS SUAS ARESTAS DESIGNADAS)
    uint64_t edges_evaluated = 0;
    uint64_t cross_edges_found = 0;
    for (size_t i = 0; i < my_edges; ++i) {
      uint32_t u = edges_to_process[i].src;
      uint32_t v = edges_to_process[i].dst;
      double w = edges_to_process[i].w;

      edges_evaluated++;
      // Obtém as raízes atuais dos componentes
      uint32_t raiz_u = find_component(u, components_local);
      uint32_t raiz_v = find_component(v, components_local);

      // Apenas consideramos arestas que conectam componentes diferentes
      if (raiz_u != raiz_v) {
        cross_edges_found++;
        // Atualiza a melhor aresta para o componente de u
        if (components_local[raiz_u].best_edge.src == INF32 ||
            components_local[raiz_u].best_edge.w > w) {
          components_local[raiz_u].best_edge = edges_to_process[i];
        }
        // Atualiza a melhor aresta para o componente de v
        if (components_local[raiz_v].best_edge.src == INF32 ||
            components_local[raiz_v].best_edge.w > w) {
          components_local[raiz_v].best_edge = edges_to_process[i];
        }
      }
    }
    logging(RANK, INFO, "Busca local concluida na iteracao %d: %lu arestas avaliadas, %lu arestas de fronteira encontradas",
            iteracao, edges_evaluated, cross_edges_found);

    // SINCRONIZAÇÃO GLOBAL DE TODAS AS MELHORES ARESTAS ENCONTRADAS VIA REDUÇÃO
    logging(RANK, INFO, "Iniciando MPI_Allreduce para consolidar as melhores arestas globalmente");
    MPI_Allreduce(components_local, components_global, vertex_len,
                  MPI_COMPONENT_T, MPI_BEST_EDGE_OP, MPI_COMM_WORLD);

    // 2.3 - SINCRONIZAÇÃO E FUSÃO (UNIÃO DOS COMPONENTES)
    logging(RANK, INFO, "Iniciando fusao de componentes e merge");
    bool arestas_adicionadas_nesta_rodada = false;
    uint32_t merges_performed = 0;

    for (size_t i = 0; i < vertex_len; ++i) {
      if (components_global[i].best_edge.src != INF32) {
        edge_t escolhida = components_global[i].best_edge;
        uint32_t raiz_u = find_component(escolhida.src, components_local);
        uint32_t raiz_v = find_component(escolhida.dst, components_local);

        if (raiz_u != raiz_v) {
          merges_performed++;
          // Rank 0 armazena a aresta na MST final
          if (RANK == 0) {
            mst_arestas[mst_quantidade_arestas++] = escolhida;
            logging(RANK, INFO, "Aresta adicionada na MST pelo Rank 0: %u -(%.6f)-> %u", escolhida.src, escolhida.w, escolhida.dst);
          }

          // Merge dos componentes
          components_local[raiz_u]._id = raiz_v;
          quantidade_componentes--;
          arestas_adicionadas_nesta_rodada = true;
        }
      }
    }
    logging(RANK, INFO, "Rodada de fusao concluida: %u fusoes realizadas, componentes restantes: %zu", merges_performed, quantidade_componentes);

    // Atualiza barra de progresso no Rank 0 para verbosidade normal
    if (RANK == 0 && verbosity >= 1) {
      float prog = 0.0f;
      if (vertex_len > 1) {
        prog = 1.0f - (float)(quantidade_componentes - 1) / (vertex_len - 1);
      } else {
        prog = 1.0f;
      }
      if (prog < 0.0f) prog = 0.0f;
      if (prog > 1.0f) prog = 1.0f;
      progress_bar_update(&pb, prog);
      fflush(stdout);
    }

    // 2.4 - CHECAGEM DE PARADA
    if (!arestas_adicionadas_nesta_rodada || quantidade_componentes <= 1) {
      logging(RANK, INFO, "Criterio de parada atendido: arestas_adicionadas=%d, componentes_restantes=%zu",
              arestas_adicionadas_nesta_rodada, quantidade_componentes);
      finalizado = true;
    }
  }

  // Garante que a barra de progresso termine com 100% e pule linha
  if (RANK == 0 && verbosity >= 1) {
    if (pb.value < 1.0f) {
      progress_bar_update(&pb, 1.0f);
      fflush(stdout);
    }
  }

  logging(RANK, INFO, "Loop concluido. Componentes finais: %zu, total de arestas na MST: %d",
          quantidade_componentes, mst_quantidade_arestas);

  // Captura término do loop
  gettimeofday(&t_loop, NULL);

  //=========================================================================
  //===                     FINALIZAÇÃO E LIMPEZA                         ===
  //=========================================================================
  logging(RANK, INFO, "Iniciando finalizacao e desalocacao de recursos");

  double peso_total = 0.0;
  if (RANK == 0 && mst_arestas) {
    for (int i = 0; i < mst_quantidade_arestas; ++i) {
      peso_total += mst_arestas[i].w;
    }
  }

  // Liberação das estruturas para capturar o tempo total até a desalocação
  if (components_global) free(components_global);
  if (components_local) free(components_local);
  if (edges_to_process) free(edges_to_process);
  MPI_Op_free(&MPI_BEST_EDGE_OP);
  MPI_Type_free(&MPI_EDGE_T);
  MPI_Type_free(&MPI_COMPONENT_T);

  // Captura o tempo total de execução final
  gettimeofday(&t_end, NULL);
  logging(RANK, INFO, "Recursos desalocados e tipos MPI liberados.");

  //=========================================================================
  //===                     APRESENTAÇÃO DE RESULTADOS                    ===
  //=========================================================================
  // Cálculo de tempos decorridos
  double time_lb = get_elapsed(t_start, t_meta_lb);
  double time_io = get_elapsed(t_meta_lb, t_io_sort);
  double time_process = get_elapsed(t_io_sort, t_loop);
  double time_total = get_elapsed(t_start, t_end);

  logging(RANK, INFO, "Tempos de execucao: Setup=%f s, I/O/Ordenacao=%f s, Processamento=%f s, Total=%f s",
          time_lb, time_io, time_process, time_total);

  // Redução dos tempos para exibir mínimo e máximo entre os ranks
  double max_time_lb = 0.0, min_time_lb = 0.0;
  double max_time_io = 0.0, min_time_io = 0.0;
  double max_time_process = 0.0, min_time_process = 0.0;

  MPI_Reduce(&time_lb, &max_time_lb, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
  MPI_Reduce(&time_lb, &min_time_lb, 1, MPI_DOUBLE, MPI_MIN, 0, MPI_COMM_WORLD);

  MPI_Reduce(&time_io, &max_time_io, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
  MPI_Reduce(&time_io, &min_time_io, 1, MPI_DOUBLE, MPI_MIN, 0, MPI_COMM_WORLD);

  MPI_Reduce(&time_process, &max_time_process, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
  MPI_Reduce(&time_process, &min_time_process, 1, MPI_DOUBLE, MPI_MIN, 0, MPI_COMM_WORLD);

  // Apresentação de saídas dependente da verbosidade no terminal do Rank 0
  if (RANK == 0) {
    if (verbosity >= 1) {
      print_box_double("ARVORE GERADORA MINIMA (MST) ENCONTRADA", TERMINAL_COLOR_GREEN);

      char info_text[512];
      snprintf(info_text, sizeof(info_text), "QUANTIDADE DE ARESTAS NA MST: %d  |  PESO TOTAL DA MST: %.9f",
               mst_quantidade_arestas, peso_total);
      print_box(info_text, TERMINAL_COLOR_CYAN);

      // Tabela de arestas (se o grafo for pequeno)
      if (vertex_len < 100 && mst_arestas) {
        TableStyle ts;
        table_init(&ts, 3, TERMINAL_COLOR_CYAN, TERMINAL_COLOR_WHITE, TERMINAL_COLOR_GREEN);
        table_set_col_width(&ts, 0, 15);
        table_set_col_width(&ts, 1, 15);
        table_set_col_width(&ts, 2, 15);

        const char *headers[] = {"Origem (src)", "Peso (w)", "Destino (dst)"};
        table_print_header(&ts, headers);

        for (int i = 0; i < mst_quantidade_arestas; ++i) {
          char src_buf[32], w_buf[32], dst_buf[32];
          snprintf(src_buf, sizeof(src_buf), "%u", mst_arestas[i].src);
          snprintf(w_buf, sizeof(w_buf), "%.2f", mst_arestas[i].w);
          snprintf(dst_buf, sizeof(dst_buf), "%u", mst_arestas[i].dst);
          const char *row[] = {src_buf, w_buf, dst_buf};
          table_print_row(&ts, row);
        }
        table_print_footer(&ts);

        // Exibe o caminho da árvore
        printf("Caminho da arvore:\n  ");
        for (int i = 0; i < mst_quantidade_arestas; ++i) {
          if (i > 0) {
            printf(" %s\u2192%s ", TERMINAL_COLOR_YELLOW, TERMINAL_COLOR_RESET);
          }
          printf("%u %s(%.2f)%s %u", mst_arestas[i].src, TERMINAL_COLOR_GREEN, mst_arestas[i].w, TERMINAL_COLOR_RESET, mst_arestas[i].dst);
        }
        printf("\n\n");
      }

      // Tabela de tempos de execução
      TableStyle ts_time;
      table_init(&ts_time, 4, TERMINAL_COLOR_BLUE, TERMINAL_COLOR_WHITE, TERMINAL_COLOR_CYAN);
      table_set_col_width(&ts_time, 0, 30);
      table_set_col_width(&ts_time, 1, 15);
      table_set_col_width(&ts_time, 2, 15);
      table_set_col_width(&ts_time, 3, 15);

      const char *time_headers[] = {"Etapa do Algoritmo", "Rank 0 (s)", "Min Ranks (s)", "Max Ranks (s)"};
      table_print_header(&ts_time, time_headers);

      char r0_buf[32], min_buf[32], max_buf[32];

      snprintf(r0_buf, sizeof(r0_buf), "%.6f", time_lb);
      snprintf(min_buf, sizeof(min_buf), "%.6f", min_time_lb);
      snprintf(max_buf, sizeof(max_buf), "%.6f", max_time_lb);
      const char *row_lb[] = {"1. Setup / Balanc. Carga", r0_buf, min_buf, max_buf};
      table_print_row(&ts_time, row_lb);

      snprintf(r0_buf, sizeof(r0_buf), "%.6f", time_io);
      snprintf(min_buf, sizeof(min_buf), "%.6f", min_time_io);
      snprintf(max_buf, sizeof(max_buf), "%.6f", max_time_io);
      const char *row_io[] = {"2. Leitura E/S & Ordenacao", r0_buf, min_buf, max_buf};
      table_print_row(&ts_time, row_io);

      snprintf(r0_buf, sizeof(r0_buf), "%.6f", time_process);
      snprintf(min_buf, sizeof(min_buf), "%.6f", min_time_process);
      snprintf(max_buf, sizeof(max_buf), "%.6f", max_time_process);
      const char *row_proc[] = {"3. Processamento MST", r0_buf, min_buf, max_buf};
      table_print_row(&ts_time, row_proc);

      snprintf(r0_buf, sizeof(r0_buf), "%.6f", time_total);
      const char *row_total[] = {"Total Geral Executado", r0_buf, "-", "-"};
      table_print_row(&ts_time, row_total);

      table_print_footer(&ts_time);

      print_success(NULL, "Arvore Geradora Minima (MST) calculada com sucesso!");
    } else {
      // verbosity == 0 (quiet mode) - only print final MST details in a single plain line to stdout
      printf("MST: arestas=%d, peso=%.9f, tempo=%.6fs\n", mst_quantidade_arestas, peso_total, time_total);
    }

    if (mst_arestas) free(mst_arestas);
  }

  MPI_Finalize();
  return 0;
}

void get_memory_stats(struct sys_mem_stats *_sms) {
  if (!_sms)
    return;

  struct sysinfo si;

  if (sysinfo(&si) == 0) {
    _sms->total = (uint64_t)si.totalram * si.mem_unit;
    _sms->free = (uint64_t)si.freeram * si.mem_unit;
    _sms->used = _sms->total - _sms->free;

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

    return;
  }

  _sms->free = _sms->used = _sms->total = 0;
}

int compare_edges(const void *a, const void *b) {
  const edge_t *edgeA = (const edge_t *)a;
  const edge_t *edgeB = (const edge_t *)b;

  if (edgeA->src < edgeB->src)
    return -1;
  if (edgeA->src > edgeB->src)
    return 1;

  if (edgeA->w < edgeB->w)
    return -1;
  if (edgeA->w > edgeB->w)
    return 1;

  return 0;
}

void reduce_best_edge_operation(void *invec, void *inoutvec, int *len,
                                MPI_Datatype *datatype) {
  component_t *in = (component_t *)invec;
  component_t *inout = (component_t *)inoutvec;

  for (int i = 0; i < *len; ++i) {
    if (in[i].best_edge.w < inout[i].best_edge.w) {
      inout[i] = in[i];
    }
  }
}

uint32_t find_component(uint32_t vertex_id, component_t *components) {
  if (components[vertex_id]._id == vertex_id) {
    return vertex_id;
  }
  components[vertex_id]._id =
      find_component(components[vertex_id]._id, components);
  return components[vertex_id]._id;
}

int can_allocate(size_t _len, size_t _size) {
  if (_len <= 0 || _size <= 0)
    return -1; // How this is possible? Its literelly UNSIGNED, just don't be
               // stupid to pass 0, but... If a programmer creates an
               // idiot-proof system, the universe creates an idiot who will
               // crash the system
  struct sys_mem_stats sms;

  get_memory_stats(&sms);

  int32_t t = sms.total * 0.8;
  int32_t n = (t - sms.used) / _size;
  size_t cost = _len * _size;

  if (cost <= (uint64_t)n) {
    return _len;
  } else if (n > 0) {
    return n;
  }

  return 0; // OMG, do you colou chiclete na cruz nigga? You cant allocate
            // nothing
}

void critical_error_logger(int _signal){
    switch ( _signal ){
        case SIGSEGV: {
            logging(*_log_error_rank, CRITICAL_ERROR, "CODE FAILURE...SEGMENTATION VIOLATION (CORE DUMP)\n\n");
        } break;
        case SIGFPE: {
            logging(*_log_error_rank, CRITICAL_ERROR, "CODE FAILURE...FLOATING-POINT EXCEPTION (CORE DUMP)\n\n");
        } break;
        case SIGILL: {
            logging(*_log_error_rank, CRITICAL_ERROR, "ILLEGAL INSTRUCTION (CORE DUMP)\n\n");
        } break;
        case SIGBUS: {
            logging(*_log_error_rank, CRITICAL_ERROR, "BUS ERROR (CORE DUMP)\n\n");
        } break;
        case SIGINT: {
            logging(*_log_error_rank, CRITICAL_ERROR, "TERMINAL INTERRUPT\n\n");
        } break;
        case SIGTERM: {
            logging(*_log_error_rank, CRITICAL_ERROR, "TERMINATION SIGNAL\n\n");
        } break;
        case SIGQUIT: {
            logging(*_log_error_rank, CRITICAL_ERROR, "TERMINAL QUIT (CORE DUMP)\n\n");
        } break;
        case SIGABRT: {
            logging(*_log_error_rank, CRITICAL_ERROR, "ABORT SIGNAL (CORE DUMP)\n\n");
        } break;
        case SIGHUP: {
            logging(*_log_error_rank, CRITICAL_ERROR, "HANGUP\n\n");
        } break;
        case SIGCHLD: {
            logging(*_log_error_rank, CRITICAL_ERROR, "CHILD STATUS CHANGED\n\n");
        } break;
        case SIGPIPE: {
            logging(*_log_error_rank, CRITICAL_ERROR, "BROKEN PIPE\n\n");
        } break;
    }
    logging(*_log_error_rank, CRITICAL_ERROR, "exit(1)\n\n");
    exit(1);
}


void logging(int _rank, log_level_t _level, const char *_fmt, ...) {
  test_op(_rank, >=, 0);
  struct timeval time;
  gettimeofday(&time, NULL);
  time.tv_sec = time.tv_sec - _t0.tv_sec;
  time.tv_usec = time.tv_usec - _t0.tv_usec;
  if (time.tv_usec < 0)
    time.tv_usec += 1000000, time.tv_sec--;

  char filename[256];
  snprintf(filename, sizeof(filename), "./logs/rank%d.txt", _rank);

  FILE *fp = fopen(filename, "a+");
  test(fp);

  char lvl[16];
  switch (_level) {
  case INFO: {
    strncpy(lvl, "INFO", sizeof(lvl));
    lvl[5] = '\0';
  } break;
  case SEND: {
    strncpy(lvl, "SEND", sizeof(lvl));
    lvl[5] = '\0';
  } break;
  case RECV: {
    strncpy(lvl, "RECV", sizeof(lvl));
    lvl[5] = '\0';
  } break;
  case WARNING: {
    strncpy(lvl, "WARNING", sizeof(lvl));
    lvl[8] = '\0';
  } break;
  case ERROR: {
    strncpy(lvl, "ERROR", sizeof(lvl));
    lvl[6] = '\0';
  } break;
  case CRITICAL_ERROR: {
    strncpy(lvl, "CRITICAL ERROR", sizeof(lvl));
    lvl[15] = '\0';
  } break;
  }

  char prefix[256];
  snprintf(prefix, sizeof(prefix), "[%ld.%ld] RANK %d | [%s] --- ", time.tv_sec,
           time.tv_usec, _rank, lvl);

  char content[2048];
  va_list args;
  va_start(args, _fmt);
  vsnprintf(content, sizeof(content), _fmt, args);
  va_end(args);

  fprintf(fp, "%s%s\n", prefix, content);

  fclose(fp);
}