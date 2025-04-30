/*
 * vim: ts=4 sw=4 et tw=0 wm=0
 *
 * libdialect - A library for computing DiAlEcT layouts:
 *                 D = Decompose/Distribute
 *                 A = Arrange
 *                 E = Expand/Emend
 *                 T = Transform
 *
 * Copyright (C) 2018  Monash University
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 * See the file LICENSE.LGPL distributed with the library.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * Author(s):   Steve Kieffer   <http://skieffer.info>
*/

#include <memory>
#include <functional>
#include <string>
#include <iostream>
#include <cmath>

#include "libvpsc/rectangle.h"
#include "libavoid/libavoid.h"

#include "libdialect/commontypes.h"
#include "libdialect/graphs.h"
#include "libdialect/peeling.h"
#include "libdialect/trees.h"
#include "libdialect/nodeconfig.h"
#include "libdialect/aca.h"
#include "libdialect/chains.h"
#include "libdialect/routing.h"
#include "libdialect/planarise.h"
#include "libdialect/faces.h"
#include "libdialect/treeplacement.h"
#include "libdialect/nearalign.h"
#include "libdialect/logging.h"
#include "libdialect/util.h"
#include "libdialect/hola.h"

#include "libdialect/mytreeplacement.h"

#include "libdialect/io.h"
#include <fstream>

using namespace dialect;

using std::string;

void dialect::doHOLA(Graph &G) {
    HolaOpts opts;
    doHOLA(G, opts);
}

//z
void saveGraphSvg(const Graph &G, const std::string &prefix, unsigned step) {
    std::string stepStr = (step < 10 ? "0" : "") + std::to_string(step);

    // Generate SVG filename
    std::string svgFile = prefix + "_" + stepStr + ".svg";

    // Write graph to SVG format
    std::string svgOutput = G.writeSvg();
    writeStringToFile(svgOutput, svgFile);

    std::cout << "Saved step " << step << ": " << svgFile << std::endl;
}

void dialect::doHOLA(Graph &G, const HolaOpts &holaOpts, Logger *logger) {

    // If there's no edges, there's nothing to do.
    if (G.getNumEdges() == 0) return;

    // Prepare logging functions in case a logger is given.
    std::function<void(Graph&, string)> log = [logger](Graph &H, string name)->void{
        if (logger!=nullptr) logger->log(H, name);
    };
    std::function<void(unsigned)> nli = [logger](unsigned ln)->void{
        if (logger != nullptr) logger->nextLoggingIndex = ln;
    };
    // Initialise a logging index.
    unsigned ln = 0;




    // We let the given graph auto-infer its own ideal edge length, based on node sizes.
    double IEL = G.getIEL(); //2 * AvgNodeDim
    // Pad nodes
    double nodePadding = holaOpts.nodePaddingScalar*IEL;
    G.padAllNodes(nodePadding, nodePadding); // add the padding to the width and height of the nodes
    // We need to dismantle (disassemble) the graph, so we begin by making a copy and we work on that instead.
    // We allocate this copy on the heap, and manage it with a shared ptr, since many of our tools
    // require that. // dynamic memory allocation at runtime
    Graph_SP Gcopy = std::make_shared<Graph>(G);



    //z
    saveGraphSvg(*Gcopy, "hola_step", 0);
    // Clear any existing connector routes, for better logging output.
    Gcopy->clearAllRoutes();  // we do not have that in the input i guess

    // Peel.
    Trees trees = peel(*Gcopy);
    // After peeling, the input graph is peeled down to its own core.
    // Ac-cor-dingly : ) we rename it...
    Graph_SP &core = Gcopy;

    log(*core, string_format("%02d_core", ln++));



    // If it's just a tree, layout and quit.
    // We recognise this case by there being exactly one tree, containing the same number of
    // nodes as the original graph.
    if (trees.size() == 1 && trees.front()->underlyingGraph()->getNumNodes() == G.getNumNodes()) {
        // Give the tree a symmetric layout.
        Tree_SP &tree = trees.front();
        tree->symmetricLayout(
            holaOpts.defaultTreeGrowthDir,
            holaOpts.treeLayoutScalar_nodeSep*IEL,
            holaOpts.treeLayoutScalar_rankSep*IEL,
            holaOpts.preferConvexTrees
        );
        // Route the edges.
        RoutingAdapter ra(Avoid::OrthogonalRouting);
        ra.router.setRoutingOption(Avoid::nudgeOrthogonalSegmentsConnectedToShapes, true);
        ra.router.setRoutingOption(Avoid::nudgeSharedPathsWithCommonEndPoint, true);
        ra.router.setRoutingParameter(Avoid::idealNudgingDistance, holaOpts.routingAbs_nudgingDistance);
        tree->addNetworkToRoutingAdapter(ra, holaOpts.wholeTreeRouting);
        ra.route();
        // Remove node padding.
        G.padAllNodes(-nodePadding, -nodePadding);
        // Set layout data in original Graph.
        tree->underlyingGraph()->setPosesInCorrespNodes(G);
        tree->underlyingGraph()->setRoutesInCorrespEdges(G);
        tree->addConstraints(G, true);
        // Done.
        return;
    } //Z: I should study that

    // Otherwise we do have a core and trees.

    //Z
    //first, compute the symmetric layout of each tree
    unsigned lns = 0;  // initialise logging sub-index
    for (Tree_SP tree : trees) {
        tree->symmetricLayout(
                holaOpts.defaultTreeGrowthDir,
                holaOpts.treeLayoutScalar_nodeSep*IEL,
                holaOpts.treeLayoutScalar_rankSep*IEL,
                holaOpts.preferConvexTrees
        );
        log(*(tree->underlyingGraph()), string_format("%02d_%02d_symm_tree", ln, lns++));
    }

        // Step 2: Process each tree
    std::map<Tree_SP, Node_SP> tree2box; //Each Tree_SP has a corresponding bounding box node
    for (Tree_SP tree : trees) {
        id_type rootID = tree->getRootNode()->id();
        Node_SP coreRoot;
        try {
            coreRoot = core->getNode(rootID);  // use the root node inside the core
        } catch (const std::out_of_range&) {
            std::cerr << "Error: Root node ID " << rootID << " not found in core graph!\n";
            continue;  // skip this tree
        }

        // Step 3: Compute bounding box for tree (excluding root)
        Node_SP bboxNode = tree->buildRootlessBox(holaOpts.defaultTreeGrowthDir);
        if (!bboxNode) {
            std::cerr << "Error: Could not compute bounding box for tree rooted at " << rootID << "!\n";
            continue;
        }

        // Step 4: Add bounding box node to the core graph
        core->addNode(bboxNode);

        // Step 5: Connect bounding box node to the root node in core
        Edge_SP bboxEdge = Edge::allocate(bboxNode, coreRoot);
        core->addEdge(bboxEdge);

        // Save mapping for later
        tree2box[tree] = bboxNode;

        // Step 6: Log progress
        log(*core, string_format("%02d_%02d_add_tree_bounding_box", ln, lns++));
    }

    // Start with a plain destress (de-stress)-- no constraints, no overlap prevention -- in order to begin
    // giving the nodes a reasonable distribution in the plane.

    ColaOptions colaOpts;
// === Step 1: Estimate base spacing factor ===
// You can make this dynamic using graph density or max degree
//    double aspectRatio =2.5;
//    double spacingFactor = 1.2; // tweakable (e.g., 1.0–1.5)
//    // Let the spacing factor vary smoothly based on how far we are from 1.0 (square)
//    //Smooth Sigmoid-like Scaling
//    double smoothFactor = 1.0 + 0.2 * ((aspectRatio - 1.0) / (aspectRatio + 1.0));  // ∈ [~0.8, ~1.2]
//    spacingFactor *= smoothFactor;
//
//
//// === Step 2: Compute Ideal Edge Length (IEL) ===
//// Use average node size and spacing factor to derive IEL
//    double avgNodeSize = Gcopy->computeAvgNodeDim(); // avg of width and height
//
////    double totalNodeArea = 0.0;
////    for (auto& rect : Gcopy->boundingBoxes) {
////        double nodeArea = rect->width() * rect->height();
////        totalNodeArea += nodeArea;
////    }
//    //IEL = avgNodeSize * spacingFactor;        // base spacing between nodes
//
//// === Step 3: Derive per-node area from IEL ===
//// This assumes nodes are roughly IEL apart, forming a grid-like layout
////since IEL is now including the AVGnodesize we can use that
//    double areaPerNode = IEL * IEL;  //avgNodeSize * avgNodeSize * spacingFactor;
//
//
//// === Step 4: Estimate total layout area needed ===
//    int numNodes = core->getNumNodes();
//    double layoutArea = areaPerNode *numNodes;
//
//// === Step 5: Choose aspect ratio (w/h) and compute layout dimensions ===
//// Example: 0.33 = tall; 2.5 = wide
//
//    double height = sqrt(layoutArea / aspectRatio)  +40;
//    double width  = aspectRatio * height  +40;
//
//   // double safetyMargin = 0.2;  // 10% extra room
//    *sharedWidth  = width  ;//* (1.0 + safetyMargin);
//    *sharedHeight = height ;//* (1.0 + safetyMargin);
//
//// === Step 7: Save to ColaOptions for layout and constraints ===
//    colaOpts.newHeight = sharedHeight;
//    colaOpts.newWidth  = sharedWidth;
//    colaOpts.idealEdgeLength = IEL;

// === Step 8: Register IEL in the graph copy ===
    //Gcopy->setIEL(IEL);

    //skeleton force
    // Parameters
    int num_ghost_per_axis = 10;
    double aspectRatio = 2.0; // e.g., wider than tall
    //double IEL = G.getIEL();
    double width = sqrt(core->getNumNodes() * IEL * IEL * aspectRatio);
    double height = width / aspectRatio;
    double centerX = width / 2.0;
    double centerY = height / 2.0;

// Add ghost nodes in a cross shape
    std::vector<Node_SP> ghostNodes;

// Horizontal (x-axis)
    for (int i = 0; i < num_ghost_per_axis; ++i) {
        double x = i * (width / (num_ghost_per_axis - 1));
        ghostNodes.push_back(core->addNode(x, centerY, 0.01, 0.01));  // small node
    }

// Vertical (y-axis)
    for (int i = 0; i < num_ghost_per_axis; ++i) {
        double y = i * (height / (num_ghost_per_axis - 1));
        ghostNodes.push_back(core->addNode(centerX, y, 0.01, 0.01));  // small node
    }
//add edges
    for (const auto& kv : core->getNodeLookup()) {
        Node_SP realNode = kv.second;
        if (realNode->getBoundingBox().w() > 0.05) {  // skip ghost nodes
            // Find nearest ghost node
            Node_SP nearest = nullptr;
            double bestDist = DBL_MAX;
            for (Node_SP ghost : ghostNodes) {
                double dx = ghost->getCentre().x - realNode->getCentre().x;
                double dy = ghost->getCentre().y - realNode->getCentre().y;
                double d2 = dx*dx + dy*dy;
                if (d2 < bestDist) {
                    bestDist = d2;
                    nearest = ghost;
                }
            }
            // Add edge with small weight (in IEL vector later)
            Edge_SP e = core->addEdge(realNode, nearest);
            // Store somewhere you can mark this edge as "light" if needed
        }
    }
    const auto& edges = core->getEdgeLookup();

    for (const auto& [eid, edge] : edges)  {
        auto dims1 = edge->getSourceEnd()->getDimensions();
        auto dims2 = edge->getTargetEnd()->getDimensions();

        bool isGhost = (dims1.first < 0.05 && dims1.second < 0.05)
                       || (dims2.first < 0.05 && dims2.second < 0.05);
        colaOpts.eLengths.push_back(isGhost ? 0.5 : 1.0);
    }

    core->destress(colaOpts); //first round of FD layout
    log(*core, string_format("%02d_free_destress_core", ln++));

    BoundingBox bbox = core->getBoundingBox();
    double width1 = bbox.w();
    double height1 = bbox.h();

    if (height1 != 0.0) {
        double aspectRatio1 = width1 / height1;
        std::cout << "Aspect Ratio: " << aspectRatio1 << " (W: " << width1 << ", H: " << height1 << ")" << std::endl;
    } else {
        std::cerr << "Warning: height is zero. Cannot compute aspect ratio." << std::endl;
    }

    //z:

    //colaOpts.preventOverlaps = true;

// Shared aspect ratio
//    double aspectRatio = *sharedWidth / *sharedHeight;
//
//// Compute the current layout's area
//    BoundingBox bboxOrig = core->getBoundingBox();
//    double OrigArea = bboxOrig.w() * bboxOrig.h();
//
//// Compute new height and width for the bounding box with the same area and desired aspect ratio
//    *sharedHeight = sqrt(OrigArea / aspectRatio);
//    *sharedWidth = aspectRatio * *sharedHeight;
//
//// Assign to ColaOptions
//    colaOpts.newHeight = sharedHeight;
//    colaOpts.newWidth = sharedWidth;
//
//    std::cout << "old H: " << bboxOrig.h() << " new H: " << *colaOpts.newHeight << std::endl;
//    std::cout << "old w: " << bboxOrig.w() << " new w: " << *colaOpts.newWidth << std::endl;
//
//// Apply layout
//    core->destress();
//    log(*core, string_format("%02d_free2_destress_core", ln++));






    //Z
//    colaOpts.aspectRatioCons =true;
//    double aspectRatio = 2.0/2.0;
//    //compute the bbox of the current layout
//    BoundingBox bboxOrig = core->getBoundingBox();
//    //compute the area of the bboxOrig
//    double OrigArea = bboxOrig.w() * bboxOrig.h();
//    //compute new h and w for the new bounding box that has same area and desired aspect ratio
//    colaOpts.newHeight = sqrt(OrigArea / aspectRatio);
//    colaOpts.newWidth = aspectRatio * colaOpts.newHeight;
//    std::cout << "old H: "<< bboxOrig.h() << "new H: "<< colaOpts.newHeight <<std::endl;
//    std::cout << "old w: "<< bboxOrig.w() << "new w: "<< colaOpts.newWidth <<std::endl;

    colaOpts.preventOverlaps = true;
    std::cout <<"non overlap is added now" << std::endl;
    // Now destress again, this time removing any node overlaps.
    core->destress(colaOpts); //second round of FD layout
    //colaOpts.aspectRatioCons =false;
    //std::cout <<core->getBoundingBox().w() << "  "<< core->getBoundingBox().h()<<std::endl;
    log(*core, string_format("%02d_OP_destress_core", ln++));

     bbox = core->getBoundingBox();
     width1 = bbox.w();
     height1 = bbox.h();

    if (height1 != 0.0) {
        double aspectRatio1 = width1 / height1;
        std::cout << "Aspect Ratio: " << aspectRatio1 << " (W: " << width1 << ", H: " << height1 << ")" << std::endl;
    } else {
        std::cerr << "Warning: height is zero. Cannot compute aspect ratio." << std::endl;
    }

    // Layout the hubs.
    nli(ln); // this for the logger
    OrthoHubLayoutOptions ohlOpts; // this could be hardest part, Because it's working very locally Just looking at one node at a time
    ohlOpts.avoidFlatTriangles = holaOpts.orthoHubAvoidFlatTriangles; // it is true initially
    OrthoHubLayout ohl(core, ohlOpts);
    ohl.layout(logger);

    log(*core, string_format("%02d_core_ortho_hub", ln++));

    bbox = core->getBoundingBox();
    width1 = bbox.w();
    height1 = bbox.h();

    if (height1 != 0.0) {
        double aspectRatio1 = width1 / height1;
        std::cout << "Aspect Ratio: " << aspectRatio1 << " (W: " << width1 << ", H: " << height1 << ")" << std::endl;
    } else {
        std::cerr << "Warning: height is zero. Cannot compute aspect ratio." << std::endl;
    }

    // Set extra gap for boundary constraints.
    core->getSepMatrix().setExtraBdryGap(IEL/2.0); // spacing between nodes

    // Dissipate (remove) any stress accumulated during ortho hub layout, aiming to regain a natrual
    // distribution for the nodes that remain unconstrained, and perhaps regain natural symmetries.
    // This time, besides just preventing overlaps between nodes, we also prevent any nodes from
    // overlapping with aligned edges.
    colaOpts.solidifyAlignedEdges = true;
    colaOpts.logger = logger;
    nli(ln);
    colaOpts.idealEdgeLength =IEL;
    core->destress(colaOpts); // another FD layout for minimizing the stress after orthogonalizing with extar gap //here the chains will be smoothed

    log(*core, string_format("%02d_EOP_destress_core", ln++));

    bbox = core->getBoundingBox();
    width1 = bbox.w();
    height1 = bbox.h();

    if (height1 != 0.0) {
        double aspectRatio1 = width1 / height1;
        std::cout << "Aspect Ratio: " << aspectRatio1 << " (W: " << width1 << ", H: " << height1 << ")" << std::endl;
    } else {
        std::cerr << "Warning: height is zero. Cannot compute aspect ratio." << std::endl;
    }

    // Next we lay out the links.
    // We may or may not build Chains for this process. Later we will need to know whether chains
    // were built, so the vector of Chains is declared at this scope.
    Chains chains;
    if (holaOpts.useACAforLinks) {
        // Use ACA.
        ACALayout aca(core);
        aca.createAlignments();
        // ACA is an older algorithm, from before we used Graphs.
        // For backward compatibility, it does not automatically update its Graph with the
        // positions and constraints from the layout, because it does not always /have/ a Graph.
        // So we ask it to do the update.
        aca.updateGraph();
    } else {
        // Use shape-conforming chain layout.
        chains = buildAllChainsInGraph(core);
        for (Chain_SP chain : chains) chain->takeShapeBasedConfiguration(); // this one,  where we ask we grab the graphs get sepMatrix.
        // We project before destressing with edge-node overlap prevention, so that the edges of
        // the chain can be axis aligned first.
        // We do NOT want overlap prevention for the projection, because the new chain configuration
        // constraints may very well reverse one or more orthogonal orderings, and we need them to
        // be free to do that.
        colaOpts.preventOverlaps = false;
        colaOpts.solidifyAlignedEdges = false;
        core->project(colaOpts, vpsc::XDIM);
        core->project(colaOpts, vpsc::YDIM);
    }

    // Destress with overlap prevention including aligned edges.
    // At this time we also prepare for the next step, which involves connector routing.
    // To ensure the routing is possible, we ensure there is some gap between all nodes.
    // We do this by adding padding, destressing, and then removing this padding.
    colaOpts.preventOverlaps = true;
    colaOpts.solidifyAlignedEdges = true;
    nli(ln);
    double preRoutingGapIELScalar = 0.125;
    double preRoutingGap = preRoutingGapIELScalar*IEL;
    core->padAllNodes(preRoutingGap, preRoutingGap);

    core->destress(colaOpts);
    core->padAllNodes(-preRoutingGap, -preRoutingGap);
    if (holaOpts.useACAforLinks) {
        log(*core, string_format("%02d_core_link_config_ACA", ln++));
    } else {
        log(*core, string_format("%02d_core_link_config_Chains", ln++));
    }

    bbox = core->getBoundingBox();
    width1 = bbox.w();
    height1 = bbox.h();

    if (height1 != 0.0) {
        double aspectRatio1 = width1 / height1;
        std::cout << "Aspect Ratio: " << aspectRatio1 << " (W: " << width1 << ", H: " << height1 << ")" << std::endl;
    } else {
        std::cerr << "Warning: height is zero. Cannot compute aspect ratio." << std::endl;
    }

    //z
    //saveGraphSvg(*core, "hola_step", ln);
    // Next is the phase in which we planarise the core.
    // However, we want a 4-planar orthogonal layout with no leaves for this phase, so we first
    // perform a special orthogonal connector routing, which ensures that no nodes will become
    // leaves in the planarisation. (It does this by ensuring that connectors are routed to at
    // least two distinct sides of each node.)
    //std::cout << "num nodes "<< core->getNumNodes()<<std::endl;
    LeaflessOrthoRouter lor(core, holaOpts); //Z not clear
    nli(ln);
    lor.route(logger);
    ++ln;

    //std::cout <<core->getBoundingBox().w() << "  "<< core->getBoundingBox().h()<<std::endl;
    log(*core, string_format("%02d_core_leafless_ortho_route", ln++));

    OrthoPlanariser op(core);
    Graph_SP P = op.planarise();

    log(*P, string_format("%02d_planar_graph_P", ln++));

    // Set extra gap for boundary constraints.
    P->getSepMatrix().setExtraBdryGap(IEL/2.0); //Z: edge to edge boundary between nodes not between centers
    log(*P, string_format("%02d_planar_graph_Extra_space_P", ln++));

    // Destress the new planar graph P, aiming to regain possible natural symmetries.
    // But use overlap prevention so that the structure cannot change.
    // (Note that now /all/ edges are aligned, so we have total edge-node overlap prevention.)
    colaOpts.preventOverlaps = true;
    colaOpts.solidifyAlignedEdges = true;
    nli(ln);
    P->destress(colaOpts);

    log(*P, string_format("%02d_P_EOP_destress", ln++));

/*
    // Now we want to reattach the trees, choosing faces of the planarised core in which to
    // place them.
    // First the trees need their own symmetric layout.
    //unsigned lns = 0;  // initialise logging sub-index
    for (Tree_SP tree : trees) {
        tree->symmetricLayout(
            holaOpts.defaultTreeGrowthDir,
            holaOpts.treeLayoutScalar_nodeSep*IEL,
            holaOpts.treeLayoutScalar_rankSep*IEL,
            holaOpts.preferConvexTrees
        );
        log(*(tree->underlyingGraph()), string_format("%02d_%02d_symm_tree", ln, lns++));
    }

    //Z: here I should do the bounding boxes for the phases and feed it to the FD layout

    ++ln;
    nli(ln);
    // Now we can choose faces and reattach them.
    //Z: different ways we can reattach trees or different ways we can orient the trees. this could have a pretty big impact on the final aspect ratio.
    FaceSet_SP faceSet = reattachTrees(P, trees, holaOpts, logger);
    ++ln;
    // We will need the vector of chosen tree placements.
    TreePlacements tps = faceSet->getAllTreePlacements();
    log(*P, string_format("%02d_with_Boxes", ++ln));
    //z
    colaOpts.preventOverlaps = true;
    colaOpts.solidifyAlignedEdges = true;
    nli(ln);
    P->destress(colaOpts);
    log(*P, string_format("%02d_with_Boxes_after_FD", ++ln));

    */
/*   Z: here I should do something to add the actual trees
 *
    // Next we insert the actual trees back into the planar graph.
    // The trees come with buffer nodes. We build a record of those, so they can be
    // ignored where necessary.
    */

    // === Manually re-insert actual trees at bounding box positions ===
    NodesById bufferNodes;
    EdgesById treeEdges;
    TreePlacements tps;

// Reuse the bounding boxes you created earlier (tree2box)
    for (auto &pair : tree2box) {
        Tree_SP tree = pair.first;
        Node_SP boxNode = pair.second;

        std::cout << "Creating placement for tree ID: "
                  << (tree ? tree->getRootNodeID() : -1) << std::endl;

        if (!tree || !boxNode) {
            std::cerr << "Error: Null tree or boxNode encountered.\n";
            continue;
        }

        try {
            TreePlacement_SP tp = std::make_shared<MyTreePlacement>(tree, boxNode);
            std::cout << "Calling applyGeometryToTree..." << std::endl;
            tp->applyGeometryToTree();
            tps.push_back(tp);
        } catch (const std::exception& e) {
            std::cerr << "Caught exception during placement: " << e.what() << std::endl;
        } catch (...) {
            std::cerr << "Unknown error during placement for tree ID: " << tree->getRootNodeID() << std::endl;
        }
    }



    std::vector<NodesById> clustersSansBufferNodes;
    for (auto tp : tps) {
        tp->applyGeometryToTree();  // sets internal positions

        NodesById treeNodes;
        NodesById buffNodes;
        tp->insertTreeIntoGraph(*P, treeNodes, buffNodes, treeEdges);

        clustersSansBufferNodes.push_back(treeNodes);

        // Combine and store clusters
        treeNodes.insert(buffNodes.begin(), buffNodes.end());
        bufferNodes.insert(buffNodes.begin(), buffNodes.end());
        colaOpts.nodeClusters.push_back(treeNodes);
    }

    //z:
    // Remove bounding box nodes and connecting edges

    for (const auto& [tree, boxNode] : tree2box) {
        for (const auto& [edgeId, edge] : boxNode->getEdgeLookup()) {
            P->severEdge(*edge);
        }
        P->removeNode(*boxNode);
    }




    colaOpts.preventOverlaps = true;
    colaOpts.solidifyAlignedEdges = true;
    nli(ln);
    P->destress(colaOpts);
    log(*P, string_format("%02d_P_with_trees", ln++));

    // We don't need solid edges within the trees; moreover, this would cause constraint
    // conflicts since the tree nodes now belong to clusters to which their solid edges
    // would not belong.
    colaOpts.solidEdgeExemptions = treeEdges;
    // Destress using neighbour stress, in order to compactify.

    colaOpts.useNeighbourStress = true;
    // Now that we are using clusters to keep the tree nodes together, we make sure
    // we do not use majorization, since ConstrainedMajorizationLayout does not work
    // with RectangularClusters.
    colaOpts.useMajorization = false;

    nli(ln);
    P->destress(colaOpts);

    log(*P, string_format("%02d_P_nbr_destress", ln++));

/*
    // Do near alignments.
    if (holaOpts.do_near_align) {
        AlignmentTable atab(*P, bufferNodes);
        for (size_t i = 0; i < holaOpts.align_reps; ++i) {
            doNearAlignments(*P, atab, bufferNodes, holaOpts);
            // After each attempt to add alignment constraints, destress, again using
            // neighbour stress and majorization.
            nli(ln);
            P->destress(colaOpts);
            log(*P, string_format("%02d_P_near_alignments", ln++));
        }
    }

    // Delete buffer nodes.
    P->removeNodes(bufferNodes);
    colaOpts.nodeClusters = clustersSansBufferNodes;
    log(*P, string_format("%02d_without_buffer_nodes", ln++));


    // Rotate if desired.
    // Z no rotation
    if (false && holaOpts.preferredAspectRatio != AspectRatioClass::NONE) {
        std::cout <<"rotete !!"<<std::endl;
        BoundingBox b = P->getBoundingBox(bufferNodes);
        double w = b.w(),
               h = b.h();
        bool scaleBySize = true;
        unsigned quarterTurnsCW = 0;
        auto counts = faceSet->getNumTreesByGrowthDir(scaleBySize);
        if ((w < h && holaOpts.preferredAspectRatio == AspectRatioClass::LANDSCAPE) ||
            (h < w && holaOpts.preferredAspectRatio == AspectRatioClass::PORTRAIT)) {
            // Need to rotate 90 degrees to get preferred aspect ratio.
            // There are two ways to do this (clockwise and anticlockwise).
            // In order to choose one, consult the preferred tree growth direction.
            // Determine how many trees grow in the two directions 90 degrees away from this one.
            CardinalDir q = holaOpts.preferredTreeGrowthDir,
                        p = Compass::cardRotateAcw90(q),
                        r = Compass::cardRotateCw90(q);
            size_t np = counts[p],
                   nr = counts[r];
            nli(ln);
            if (np >= nr) {
                // In this case rotating clockwise will put more trees in the preferred
                // growth direction.
                P->rotate90cw(&colaOpts);
                // We need to rotate the constraints in the core too, since later we're
                // going to write those into the original graph.
                core->getSepMatrix().transform(SepTransform::ROTATE90CW);
                quarterTurnsCW = 1;
            } else {
                // In this case rotating anticlockwise is preferred.
                P->rotate90acw(&colaOpts);
                core->getSepMatrix().transform(SepTransform::ROTATE90ACW);
                quarterTurnsCW = 3;
            }
            ln += 2;
        } else {
            // In this case we may rotate 180 degrees if that would put more trees in the preferred
            // growth direction.
            CardinalDir q = holaOpts.preferredTreeGrowthDir,
                        s = Compass::cardFlip(q);
            size_t nq = counts[q],
                   ns = counts[s];
            if (ns > nq) {
                P->rotate180();
                core->getSepMatrix().transform(SepTransform::ROTATE180);
                quarterTurnsCW = 2;
            }
        }
        // Update Tree growth directions as needed.
        if (quarterTurnsCW != 0) {
            for (Tree_SP tree : trees) tree->rotateGrowthDirCW(quarterTurnsCW);
        }
    }

    log(*P, string_format("%02d_P_rotation", ln++));

    // Translate if desired.
    if (holaOpts.putUlcAtOrigin) {
        NodesById ignore; // leave empty; don't ignore any nodes
        bool includeBends = true; // we want the edge routes included
        BoundingBox b = P->getBoundingBox(ignore, includeBends);
        double dx = -b.x,
               dy = -b.y;
        P->translate(dx, dy);
    }
*/
    log(*P, string_format("%02d_P_translation", ln++));

    // At this point, we can ask the planar graph P to set node positions in the original graph G.
    // This is because it is now true that for every node u in G, there is a node v in P with v.ID == u.ID.
    // Initially, P had a GhostNode representing each Node in the core of G. But now we have also added
    // the Trees into P (by calling insertTreeIntoGraph on each TreePlacement). P may have additional nodes
    // that do not correspond to any nodes in G, but this does not matter.
    P->setPosesInCorrespNodes(G);
    // Similarly, P now holds the full set of constraints that we want to keep, so we can ask it to set
    // those into G as well.
    G.clearAllConstraints();
    core->setCorrespondingConstraints(G);
    P->setCorrespondingConstraints(G);
    // Set extra gap for boundary constraints.
    G.getSepMatrix().setExtraBdryGap(IEL/2.0);

    log(*P, string_format("%02d_before_clearing", ln++));

    // Final connector routing.
    G.clearAllRoutes();

    if (chains.size() > 0) {
        // If we used Chains, then there may be AestheticBends that were set as route points for certain
        // connectors. For these we made Nodes and added them to the core graph. We then made GhostNodes of
        // these, in the planar graph P. In subsequent layout of P, the positions of these GhostNodes were updated.
        // However, the Chains themselves still retain pointers not to these GhostNodes, but to the original
        // AestheticBend nodes that were added to the core graph. Therefore before we can ask the Chains to add
        // these route points into the Edges of the original Graph G, we must ask P to update their positions.
        P->setPosesInCorrespNodes(*core);
        for (Chain_SP ch : chains) ch->addAestheticBendsToEdges();
        G.buildRoutes();
    }

    log(*P, string_format("%02d_final_1", ln++));

    // Set up a routing adapter.
    RoutingAdapter ra(Avoid::OrthogonalRouting);
    ra.router.setRoutingOption(Avoid::nudgeOrthogonalSegmentsConnectedToShapes, true);
    ra.router.setRoutingOption(Avoid::nudgeSharedPathsWithCommonEndPoint, true);
    ra.router.setRoutingParameter(Avoid::crossingPenalty, 2*IEL);
    ra.router.setRoutingParameter(Avoid::segmentPenalty, IEL/2.0);
    ra.router.setRoutingParameter(Avoid::idealNudgingDistance, holaOpts.routingAbs_nudgingDistance);
    // Ask the core graph to add its nodes, and just those edges that do not have any bend nodes.
    // After asking G to clear all routes (remember G has the same Edges as core), these will be all and only
    // those Edges for which no Chain set any aesthetic bend.
    
    // Remove part of the node padding now, to ensure open channels for connector routing.
    double nodePaddingLayer1 = 2*preRoutingGapIELScalar*nodePadding;
    double nodePaddingLayer2 = nodePadding - nodePaddingLayer1;
    core->padAllNodes(-nodePaddingLayer1, -nodePaddingLayer1);
    
    core->addBendlessSubnetworkToRoutingAdapter(ra);
    log(G, string_format("%02d_final_2", ln++));

    // Ask each Tree to add its network to the router.
    for (Tree_SP tree : trees) {
        tree->underlyingGraph()->padAllNodes(-nodePaddingLayer1, -nodePaddingLayer1);
        tree->padCorrespNonRootNodes(G, -nodePaddingLayer1, -nodePaddingLayer1);
        tree->addNetworkToRoutingAdapter(ra, holaOpts.peeledTreeRouting, core);
    }
    // Do the routing.
    ra.route();
    // Again, since the Edges of core also belong to G, those routes are already set in the original graph G.
    // However the Edges of the Trees are new ones that were never in G. So we need to set those routes.
    for (Tree_SP tree : trees) {
        tree->underlyingGraph()->setRoutesInCorrespEdges(G);
    }
    log(G, string_format("%02d_final_3", ln++));

    // Remove remaining node padding.
    G.padAllNodes(-nodePaddingLayer2, -nodePaddingLayer2);


}
