#pragma once
#include <string>
#include <vector>
#include <valarray>
#include "libdialect/graphs.h"        // your main Graph class
#include "libdialect/logging.h"       // optional

namespace dialect {

    class LayoutMetrics {
    public:
        LayoutMetrics(Graph& g, double targetAR);

        // Core computations
        double computeAspectRatio(Graph &graph) const;
        double computeARError(Graph &graph) const;
        double computeStress(Graph &G);
        double computeKslStress(Graph &G);
        double computeEdgeCrossings() const;
        double computeEdgeLengthDeviation() const;
        double computeEdgeOrthogonality() const;
        double computeCrossingAngle() const;
        double computeNeighbourhoodPreservation();
        double computeNodeResolution() const;
        double computeNodeUniformity() const;
        double computeNumBends() const;
        double computeDensity(Graph &graph) const;
        double computeRuntime(double startTime, double endTime) const;

        // Graph-level descriptors
        int getNumNodes(Graph &graph) const;
        int getNumEdges(Graph &graph) const;
        int getMaxDegree(Graph &graph) const;

        void computeMetrics(Graph&G, double startTime, double endTime) ;
        void printSummary() const;
        void writeCSV();


    private:

        double targetAspectRatio;
        // store results
        int numNodes, numEdges, maxDegree, numBends;
        double density, runtime;
        double finalAR, arError;
        double stress, kslStress, edgeCross, edgeLengthDev;
        double edgeOrth, crossAngle, neighPres;
        double nodeRes, nodeUni;
    };

} // namespace dialect
