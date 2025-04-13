//
// Created by alsuwazj on 4/10/2025.
//

#ifndef HOLACMAKE_MYTREEPLACEMENT_H
#define HOLACMAKE_MYTREEPLACEMENT_H

#pragma once

#include "libdialect/treeplacement.h"
#include "libdialect/trees.h"
#include "libdialect/graphs.h"
#include "libavoid/geomtypes.h"

namespace dialect {

    class MyTreePlacement : public TreePlacement {
        Tree_SP tree;
        Node_SP boxNode;

        static std::shared_ptr<Graph> &getDummyGraph() {
            static std::shared_ptr<Graph> dummyGraph = std::make_shared<Graph>();
            return dummyGraph;
        }

        static Face &getDummyFace() {
            static Face dummyFace(getDummyGraph());
            return dummyFace;
        }

        static Node_SP &getDummyNode() {
            static Node_SP dummyNode = Node::allocate();
            return dummyNode;
        }

    public:
        MyTreePlacement(Tree_SP t, Node_SP b)
                : TreePlacement(t, getDummyFace(), getDummyNode(),
                                CompassDir::EAST, CardinalDir::NORTH),
                  tree(t), boxNode(b) {
            std::cout << "Constructed MyTreePlacement for tree: "
                      << (t ? t->getRootNodeID() : -1) << std::endl;
        }

        // Do NOT add 'override' unless base has 'virtual'
        void applyGeometryToTree() override {
            if (!tree) {
                std::cerr << "applyGeometryToTree: Tree is null!" << std::endl;
                return;
            }

            if (!boxNode) {
                std::cerr << "applyGeometryToTree: boxNode is null!" << std::endl;
                return;
            }

            Node_SP root = tree->getRootNode();
            if (!root) {
                std::cerr << "applyGeometryToTree: Root node is null!" << std::endl;
                return;
            }

            Avoid::Point rootPos = root->getCentre();

            auto ug = tree->underlyingGraph();
            if (!ug) {
                std::cerr << "applyGeometryToTree: underlyingGraph is null!" << std::endl;
                return;
            }

            const NodesById& nodeMap = ug->getNodeLookup();
            if (nodeMap.empty()) {
                std::cerr << "applyGeometryToTree: nodeMap is empty!" << std::endl;
                return;
            }

            // Initialize bounding box
            double xmin = std::numeric_limits<double>::max();
            double xmax = std::numeric_limits<double>::lowest();
            double ymin = std::numeric_limits<double>::max();
            double ymax = std::numeric_limits<double>::lowest();

            for (const auto& [id, node] : nodeMap) {
                if (!node) {
                    std::cerr << "applyGeometryToTree: Found null node in nodeMap!" << std::endl;
                    continue;
                }

                if (id == root->id()) continue;

                Avoid::Point center = node->getCentre();
                double halfW = node->getDimensions().first / 2.0;
                double halfH = node->getDimensions().second / 2.0;

                xmin = std::min(xmin, center.x - halfW);
                xmax = std::max(xmax, center.x + halfW);
                ymin = std::min(ymin, center.y - halfH);
                ymax = std::max(ymax, center.y + halfH);
            }

            Avoid::Point nonRootCenter((xmin + xmax) / 2.0, (ymin + ymax) / 2.0);
            Avoid::Point boxCenter = boxNode->getCentre();
            Avoid::Point delta = boxCenter - nonRootCenter;

            tree->translate(delta);
        }





        void insertTreeIntoGraph(Graph &G,
                                 NodesById &treeNodes,
                                 NodesById &bufferNodes,
                                 EdgesById &treeEdges) override{
            tree->addNetwork(G, treeNodes, treeEdges);
            tree->addConstraints(G, true);  // alignRoot = true
            tree->addBufferNodesAndConstraints(G, bufferNodes);
        }
    };

}  // namespace dialect


#endif //HOLACMAKE_MYTREEPLACEMENT_H
