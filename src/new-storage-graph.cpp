// PGASGraph
#include <pgas-graph/graph-utilities.hpp>
#include <pgas-graph/pgas-graph.h>

// CopyPasted
#include <pgas-graph/cppmemusage.hpp>

// STL
#include <chrono>
#include <iostream>
#include <random>

// Boost
#include <boost/graph/adjacency_list.hpp>
#include <boost/graph/connected_components.hpp>
#include <boost/graph/erdos_renyi_generator.hpp>
#include <boost/graph/graphviz.hpp>
#include <boost/graph/properties.hpp>
#include <boost/graph/small_world_generator.hpp>
#include <boost/program_options.hpp>
#include <boost/random/linear_congruential.hpp>

// System
#include <limits.h>
#include <unistd.h>

// Usings
namespace po = boost::program_options;

po::options_description createProgramOptions() {
  po::options_description desc("Allowed options");
  desc.add_options()("help", "produce help message")(
      "vertex-count", po::value<int>(),
      "set vertex count")("degree", po::value<int>(), "set degree")(
      "percentage", po::value<double>(), "set connectivity percentage")
      ("print-local", po::value<int>(), "print edges on the current node");
  return desc;
}

using MyBundledVertex = PGASGraph::Graph<std::string, size_t>::Vertex;
using MyBundledEdge =
    PGASGraph::Graph<std::string, size_t>::Vertex::weight_node_t;

std::ostream &operator<<(std::ostream &os, const MyBundledVertex &e) noexcept {
  os << e.id;
  return os;
}

std::ostream &operator<<(std::ostream &os, const MyBundledEdge &e) noexcept {
  os << e.first;
  return os;
}

bool operator==(const MyBundledVertex &lhs,
                const MyBundledVertex &rhs) noexcept {
  return lhs.id == rhs.id;
}

bool operator!=(const MyBundledVertex &lhs,
                const MyBundledVertex &rhs) noexcept {
  return !(lhs == rhs);
}

template <typename Graph, typename BundledVertex>
typename boost::graph_traits<Graph>::vertex_descriptor
add_bundled_vertex(const BundledVertex &v, Graph &g) noexcept {
  static_assert(!std::is_const<Graph>::value, "graph cannot be const");
  const auto vd = boost::add_vertex(g);
  g[vd] = v;
  return vd;
}

template <typename Graph> void set_my_bundled_vertexes(Graph &g) {
  static_assert(!std::is_const<Graph>::value, "graph cannot be const");
  const auto vip = boost::vertices(g);
  const auto j = vip.second;

  PGASGraph::Id id{0};
  for (auto i = vip.first; i != j; ++i) {
    g[*i] = MyBundledVertex(id++);
  }
}

template <typename Graph, typename Function>
void for_each_bundled_vertex(Graph &g, Function f) {
  static_assert(!std::is_const<Graph>::value, "graph cannot be const");
  const auto vip = boost::vertices(g);
  const auto j = vip.second;

  PGASGraph::Id id{0};
  for (auto i = vip.first; i != j; ++i) {
    f(g[*i]);
  }
}

using PGASGraphType = PGASGraph::Graph<std::string, size_t>;
size_t generateRandomConnectedPGASGraph(const size_t vertexCount,
                                        double percentage,
                                        PGASGraphType &graph) {
  using PId = PGASGraph::Id;
  const auto rank_me {upcxx::rank_me()};
  const auto rank_n {upcxx::rank_n()};
  std::list<PId> unconnected;
  std::vector<PId> connected;
  connected.reserve(vertexCount);

  size_t extraEdges{static_cast<size_t>((vertexCount * vertexCount / 2) *
                                        (percentage / 100.0))};

  size_t edges{0};

  std::random_device rnd;
  std::mt19937 gen{rnd()};

  const auto minId = rank_me * vertexCount;
  const auto maxId = (rank_me + 1) * vertexCount - 1;

  connected.push_back(minId);
  for (int i = minId + 1; i <= maxId; ++i) {
    unconnected.push_back(i);
  }

  // std::uniform_int_distribution<int> dist(minId, maxId);

  PGASGraph::logMsg("min=" + std::to_string(minId) +
                    ", max=" + std::to_string((maxId)));

  size_t weight{1};
  while (!unconnected.empty()) {
    const int temp1 = std::uniform_int_distribution<int>(0, connected.size())(gen);
    const int temp2 = std::uniform_int_distribution<int>(0, unconnected.size() - 1)(gen);

    PGASGraph::Id u{connected[temp1]};
    if (u < minId || u > maxId) {
      continue;
    }

    auto unconnectedItemIt{unconnected.begin()};
    std::advance(unconnectedItemIt, temp2);
    assert(unconnectedItemIt != unconnected.end());
    auto unconnectedItem{*unconnectedItemIt};
    assert(unconnectedItem != -1);
    if (unconnectedItem < minId || unconnectedItem > maxId) {
      continue;
    }

    PId v{unconnectedItem};

    if (u != v) {
      const auto parentu = graph.getVertexParent(u);
      const auto parentv = graph.getVertexParent(v);
      graph.AddEdge({u, v, weight++});
      edges++;
    }

    unconnected.erase(
        std::find(unconnected.begin(), unconnected.end(), unconnectedItem));
    connected.push_back(v);
  }

  auto copyExtraEdges {extraEdges};
  while (copyExtraEdges != 0) {
    PId temp1 = std::uniform_int_distribution<int>(minId, maxId)(gen);
    PId temp2 = std::uniform_int_distribution<int>(minId, maxId)(gen);
    if (temp1 != temp2) {
      graph.AddEdge({temp1, temp2, weight++});
      edges++;
      copyExtraEdges--;
    }
  }

  auto genIdForPartition = [&gen](const auto rank, const auto vertexCount) {
    auto minId = rank * vertexCount;
    auto maxId = (rank + 1) * vertexCount - 1;
    return std::uniform_int_distribution<PId>(minId, maxId)(gen);
  };

  for (PGASGraph::Rank r1 = 0; r1 < rank_n; ++r1) {
    for (PGASGraph::Rank r2 = 0; r2 < rank_n; ++r2) {
      copyExtraEdges = rank_n + vertexCount * 5 / 100;
      while (copyExtraEdges != 0) {
        auto idR1 {genIdForPartition(r1, vertexCount)};
        auto idR2 {genIdForPartition(r2, vertexCount)};
        if (idR1 != idR2) {
          graph.AddEdge({idR1, idR2, weight++});
          --copyExtraEdges;
        }
      }
    }
  }

  return edges;
}

int main(int argc, char *argv[]) {
  // Parse command line options.
  auto optionsDesc = createProgramOptions();
  po::variables_map vm;
  po::store(po::parse_command_line(argc, argv, optionsDesc), vm);
  if (vm.count("help")) {
    std::cout << optionsDesc << std::endl;
    return 1;
  }

  // Get vertex count.
  size_t totalNumberVertices = 256;
  if (vm.count("vertex-count")) {
    totalNumberVertices = vm["vertex-count"].as<int>();
  }

  // Get vertex count.
  size_t degree = 10;
  if (vm.count("degree")) {
    degree = vm["degree"].as<int>();
  }

  // Get vertex count.
  double percentage = 5.0;
  if (vm.count("percentage")) {
    percentage = vm["percentage"].as<double>();
  }

  // Get vertex count.
  bool printLocal = false;
  if (vm.count("print-local")) {
    printLocal = vm["print-local"].as<int>();
  }

  upcxx::init();
  // Distribute vertices over the ranks.
  const size_t verticesPerRank =
      (totalNumberVertices + upcxx::rank_n() - 1) / upcxx::rank_n();

  PGASGraphType pgasGraph(totalNumberVertices, verticesPerRank);
  const auto rank_n = upcxx::rank_n();
  for (size_t r = 0; r < rank_n; ++r) {
    if (r == upcxx::rank_me()) {
      auto start = std::chrono::steady_clock::now();
      auto edgeCount = generateRandomConnectedPGASGraph(verticesPerRank,
                                                        percentage, pgasGraph);
      auto end = std::chrono::steady_clock::now();
      PGASGraph::logMsg(
          "Graph generation elapsed time: " +
          std::to_string(std::chrono::duration<double>(end - start).count()));
    }
  }

  upcxx::barrier();

  pgasGraph.ExportIntoFile("serialized.txt");
  if (printLocal) {
    pgasGraph.printLocal();
  }

  upcxx::finalize();
  return 0;
};
