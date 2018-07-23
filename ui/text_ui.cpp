#include "absl/memory/memory.h"
#include "enumerable/clique.hpp"
#include "enumerable/diam2kplex.hpp"
//#include "enumerator/parallel_tbb.hpp"
#include "enumerator/sequential.hpp"
//#include "enumerator/parallel_pthreads.hpp"
#include "enumerator/parallel_pthreads_steal.hpp"
#ifdef PARALLELENUM_USE_MPI
#include "enumerator/distributed_mpi.hpp"
#endif

#include <thread>
#include "gflags/gflags.h"

DEFINE_string(enumerator, "sequential",
              "which enumerator should be used. Possible values: sequential, "
              "parallel, distributed");
DEFINE_int32(
    n, std::thread::hardware_concurrency(),
    "number of threads to be used on each computing node "
    "(default: number of available cores)");  // TODO: da capire come
                                              // renderlo valido solo in
                                              // caso di
                                              // enumerator=parallel
DEFINE_int32(chunks_per_node, 100,
             "number of roots chunks "
             "to be scheduled to each computing node "
             "(only valid for distributed case) (default: 100)");
DEFINE_int32(k, 2, "value of k for the k-plexes");
DEFINE_int32(q, 1, "only find diam-2 kplexes at least this big");

DEFINE_string(system, "d2kplex",
              "what should be enumerated. Possible values: clique, d2kplex");
DEFINE_string(graph_format, "nde",
              "format of input graphs. Only makes sense for systems defined on "
              "graphs. Possible values: nde, oly");
DEFINE_bool(fast_graph, true,
            "use the faster but more memory hungry graph format");
DEFINE_bool(huge_graph, false, "use 64 bit integers to count nodes");
DEFINE_bool(one_based, false,
            "whether the graph is one based. Used only by oly format.");
DEFINE_bool(quiet, false, "do not show any non-fatal output");
DEFINE_bool(enable_pivoting, true, "enable pivoting in d2kplex");

bool ValidateEnumerator(const char* flagname, const std::string& value) {
  if (value == "sequential" || value == "parallel" || value == "distributed") {
    return true;
  }
  printf("Invalid value for --%s: %s\n", flagname, value.c_str());
  return false;
}
DEFINE_validator(enumerator, &ValidateEnumerator);

bool ValidateSystem(const char* flagname, const std::string& value) {
  if (value == "clique" || value == "d2kplex") {
    return true;
  }
  printf("Invalid value for --%s: %s\n", flagname, value.c_str());
  return false;
}
DEFINE_validator(system, &ValidateSystem);

bool ValidateGraphFormat(const char* flagname, const std::string& value) {
  if (value == "nde" || value == "oly") {
    return true;
  }
  printf("Invalid value for --%s: %s\n", flagname, value.c_str());
  return false;
}
DEFINE_validator(graph_format, &ValidateGraphFormat);

template <typename node_t, typename label_t>
std::unique_ptr<fast_graph_t<node_t, label_t>> ReadFastGraph(
    const std::string& input_file, bool directed = false) {
  FILE* in = fopen(input_file.c_str(), "re");
  if (!in) throw std::runtime_error("Could not open " + input_file);
  if (FLAGS_graph_format == "nde") {
    return ReadNde<node_t, fast_graph_t>(in, directed);
  }
  if (FLAGS_graph_format == "oly") {
    return FLAGS_fast_graph
               ? ReadOlympiadsFormat<node_t, label_t, fast_graph_t>(
                     in, directed, FLAGS_one_based)
               : ReadOlympiadsFormat<node_t, label_t, fast_graph_t>(
                     in, directed, FLAGS_one_based);
  }
  throw std::runtime_error("Invalid format");
}

template <typename Node, typename Item>
std::unique_ptr<Enumerator<Node, Item>> MakeEnumerator() {
  if (FLAGS_enumerator == "sequential") {
    return absl::make_unique<Sequential<Node, Item>>();
  } else if (FLAGS_enumerator == "parallel") {
    return absl::make_unique<ParallelPthreadsSteal<Node, Item>>(FLAGS_n);
  } else if (FLAGS_enumerator == "distributed") {
#ifdef PARALLELENUM_USE_MPI
    return absl::make_unique<DistributedMPI<Node, Item>>(FLAGS_n,
                                                         FLAGS_chunks_per_node);
#else
    throw std::runtime_error(
        "To run distributed version, run "
        "again the ./build.py script, building the "
        "MPI support.");
#endif
  }
  throw std::runtime_error("Invalid enumerator");
}

template <typename node_t>
int CliqueMain(const std::string& input_file) {
  auto enumerator =
      MakeEnumerator<CliqueEnumerationNode<node_t>, Clique<node_t>>();
  auto graph = ReadFastGraph<node_t, void>(input_file);
  enumerator->ReadDone();
  enumerator->template MakeEnumerableSystemAndRun<
      CliqueEnumeration<fast_graph_t<node_t, void>>>(graph.get());
  if (!FLAGS_quiet) {
    enumerator->PrintStats();
  }
  return 0;
}

template <typename node_t>
int D2KplexMain(const std::string& input_file) {
  auto enumerator = MakeEnumerator<Diam2KplexNode<fast_graph_t<node_t, void>>,
                                   Kplex<node_t>>();
  auto graph = ReadFastGraph<node_t, void>(input_file);
  enumerator->ReadDone();
  enumerator->template MakeEnumerableSystemAndRun<
      Diam2KplexEnumeration<fast_graph_t<node_t, void>>>(
      graph.get(), FLAGS_k, FLAGS_q, FLAGS_enable_pivoting);
  if (!FLAGS_quiet) {
    enumerator->PrintStats();
  }
  return 0;
}

int main(int argc, char** argv) {
  gflags::SetUsageMessage(
      "Enumerates the maximal elements in a set system defined by a graph "
      "or other structures.");
  gflags::SetVersionString("0.1");
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  if (FLAGS_system == "clique") {
    if (argc != 2) {
      fprintf(stderr, "You should specify exactly one graph\n");
      return 1;
    }
    return FLAGS_huge_graph ? CliqueMain<uint64_t>(argv[1])
                            : CliqueMain<uint32_t>(argv[1]);
  }
  if (FLAGS_system == "d2kplex") {
    if (argc != 2) {
      fprintf(stderr, "You should specify exactly one graph\n");
      return 1;
    }
    return FLAGS_huge_graph ? D2KplexMain<uint64_t>(argv[1])
                            : D2KplexMain<uint32_t>(argv[1]);
  }
}
