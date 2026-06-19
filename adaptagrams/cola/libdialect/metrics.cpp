#include "metrics.h"
#include <cmath>
#include <fstream>
#include <iostream>
#include <algorithm>
#include <numeric>
#include <chrono>


using namespace dialect;

// --------------------------------------------------
// Constructor
// --------------------------------------------------
LayoutMetrics::LayoutMetrics(Graph& g, double targetAR)
    :  targetAspectRatio(targetAR)
{
    numNodes = getNumNodes(g);
    numEdges = getNumEdges(g);
    maxDegree = getMaxDegree(g);
    numBends = 0;
    density = 0.0;
    runtime = 0.0;
    finalAR = arError = stress = kslStress = edgeCross = edgeLengthDev = 0.0;
    edgeOrth = crossAngle = neighPres = nodeRes = nodeUni = 0.0;
}

// --------------------------------------------------
// Graph descriptors
// --------------------------------------------------
int LayoutMetrics::getNumNodes(Graph &graph) const { return graph.getNumNodes(); }
int LayoutMetrics::getNumEdges(Graph &graph) const { return graph.getNumEdges(); }

int LayoutMetrics::getMaxDegree(Graph &graph) const {
    int maxD = 0;
    for (const auto& [id, node] : graph.getNodeLookup()) {
        maxD = std::max(maxD, (int)node->getDegree());
    }
    return maxD;
}

// --------------------------------------------------
//  Metrics
// --------------------------------------------------

double LayoutMetrics::computeAspectRatio(Graph &graph) const {
    BoundingBox bbox = graph.getBoundingBox();
    double width = bbox.w();
    double height = bbox.h();

    if (height != 0.0) {
        double aspectRatio = width / height;
        return aspectRatio;
    }

}

double LayoutMetrics::computeARError(Graph &graph) const {
    double curAR = computeAspectRatio(graph);
    return std::abs(curAR - targetAspectRatio) / targetAspectRatio;
}

// Placeholder: replace with your stress model from libcola
double LayoutMetrics::computeStress(Graph &G) {
    std::cout <<"getting stree"<<std::endl;
    return G.getStress();
}

// double LayoutMetrics::computeKslStress(Graph &G) {
//     return G.getKruskalStress();
// }





double LayoutMetrics::computeCrossingAngle() const {
    // Optional: placeholder
    return 0.0;
}

double LayoutMetrics::computeNeighbourhoodPreservation() {
    // Placeholder: compute correlation between graph-distance & Euclidean
    return 0.0;
}



double LayoutMetrics::computeNumBends() const { return 0.0; }
double LayoutMetrics::computeDensity(Graph &graph) const {
    double N = (double)getNumNodes(graph);
    return (2.0 * getNumEdges(graph)) / (N * (N - 1.0));
}

double LayoutMetrics::computeRuntime(double start, double end) const {
    return (end - start);
}

// --------------------------------------------------
// Combined update / logging
// --------------------------------------------------
void LayoutMetrics::computeMetrics(Graph &G, double startTime, double endTime) {
    runtime = computeRuntime(startTime, endTime);
    finalAR = computeAspectRatio(G);
    arError = computeARError(G);
    //stress = computeStress(G);
    // kslStress = computeKslStress(G);
    // edgeCross = computeEdgeCrossings();
    // edgeLengthDev = computeEdgeLengthDeviation();
    // edgeOrth = computeEdgeOrthogonality();
    // density = computeDensity();
}


void LayoutMetrics::printSummary() const {
    std::cout << "Aspect Ratio: " << finalAR
              << " | AR error: " << arError
              << " | Stress: " << stress
              << " | Crossings: " << edgeCross
              << " | Edge Orthogonality: " << edgeOrth
              << " | Density: " << density
              << std::endl;
}

void LayoutMetrics::writeCSV() {

    logMetrics << GraphName << "," << numNodes << "," << numEdges << "," << density << ","
        << maxDegree << "," <<targetAspectRatio << "," << finalAR << "," << arError << ","
        <<stress << "," << edgeCross << "," << edgeLengthDev << ","
        << nodeRes << "," << nodeUni << "," << runtime << "\n";

    // logMetrics << "graph,nodes,edges,density,maxDegree,targetAR,finalAR,AR_error,stress,"
    //                         << "edgeCross,edgeLenDev,nodeRes,nodeUni,runtime \n";
}
