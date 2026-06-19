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
#include <random>
#include <limits>
#include <vector>

#include "libvpsc/rectangle.h"
#include "libavoid/libavoid.h"

#include "libcola/cola.h"

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
#include <chrono>

using namespace dialect;

using std::string;
auto timeNow = std::chrono::high_resolution_clock::now;


void dialect::doHOLA(Graph &G) {
    HolaOpts opts;
    doHOLA(G, opts);
}
// void printDuration(const std::string& label,
//                    std::chrono::high_resolution_clock start,
//                    std::chrono::high_resolution_clock end) {
//     auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
//     std::cout << label << " took " << duration << " ms" << std::endl;
// }
void dialect::printAspectRatio(Graph &G,  const std::string& label ) {

    BoundingBox bbox = G.getBoundingBox();
    double width = bbox.w();
    double height = bbox.h();

    if (height != 0.0) {
        double aspectRatio = width / height;
        std::cout << (label.empty() ? "" : label + ": ")
                  << "Aspect Ratio: " << aspectRatio
                  << " (W: " << width << ", H: " << height << ")" << std::endl;
    }
}

double computeAspectRatio(Graph &G ) {

    BoundingBox bbox = G.getBoundingBox();
    double width = bbox.w();
    double height = bbox.h();

    if (height != 0.0) {
        double aspectRatio = width / height;
        return aspectRatio;
    }


}


static void scaleCoreToTargetAR(Graph& core, double targetAR = GLOBAL_ASPECT_RATIO)
{

    const auto& byId = core.getNodeLookup();

    // collect bounding-box centres
    struct BB { id_type id; BoundingBox bb; double cx, cy; };
    std::vector<BB> nodes;
    nodes.reserve(byId.size());

    double minX=1e9, maxX=-1e9, minY=1e9, maxY=-1e9;
    for (const auto& [id, node] : byId) {
        BoundingBox r = node->getBoundingBox();
        double cx = 0.5*(r.x + r.X);
        double cy = 0.5*(r.y + r.Y);
        nodes.push_back({id, r, cx, cy});
        minX = std::min(minX, cx); maxX = std::max(maxX, cx);
        minY = std::min(minY, cy); maxY = std::max(maxY, cy);
    }
    if (nodes.empty()) return;

    // compute current AR (choose variance or bounding-box)
    double width = maxX - minX, height = maxY - minY;
    double curAR = std::max(1e-9, width / height);

    //variance might underestimate so it might not be the best option for the final tuning
    // double meanX=0, meanY=0;
    // for (auto& n: nodes) { meanX+=n.cx; meanY+=n.cy; }
    // meanX/=nodes.size(); meanY/=nodes.size();
    //
    // double varX=0, varY=0;
    // for (auto& n: nodes) {
    //     varX += (n.cx - meanX)*(n.cx - meanX);
    //     varY += (n.cy - meanY)*(n.cy - meanY);
    // }
    // varX /= nodes.size(); varY /= nodes.size();
    // double curAR = std::sqrt(varX) / std::sqrt(varY);

    // skip if close
    double arDiff = std::abs(curAR - targetAR) / targetAR;
    //if (arDiff < 0.15) return; //within 15% no correction

    // compute correction
    double sx = 1.0, sy = 1.0;
    const bool areaPreserve = true;  //
    const double alpha = 0.3;        // Instead of jumping straight to the target aspect ratio, apply only a fraction of the required correction (30%)
    const double MAX_CORR = 0.20;    // If the AR mismatch is large, limit scaling to at most ±20 % per correction.

    if (areaPreserve) {
        double full = std::sqrt(targetAR / curAR);
        double blended = std::pow(full, alpha); // soft correction
        double limited = std::clamp(blended, 1.0 - MAX_CORR, 1.0 + MAX_CORR);
        sx = full;
        sy = 1.0 / sx;
    } else { //this stretch in one direction
        if (targetAR > curAR) {
            double full = targetAR / curAR;
            sx = std::pow(full, alpha);
            sx = std::min(sx, 1.0 + MAX_CORR);
        } else if (targetAR < curAR) {
            double full = curAR / targetAR;
            sy = std::pow(full, alpha);
            sy = std::min(sy, 1.0 + MAX_CORR);
        }
    }

    // center for scaling
    double gx = 0.5 * (minX + maxX);
    double gy = 0.5 * (minY + maxY);

    for (const auto& n : nodes) {
        double cxNew = gx + (n.cx - gx) * sx;
        double cyNew = gy + (n.cy - gy) * sy;
        double halfW = 0.5 * (n.bb.X - n.bb.x);
        double halfH = 0.5 * (n.bb.Y - n.bb.y);

        BoundingBox newBB{cxNew - halfW, cxNew + halfW, cyNew - halfH, cyNew + halfH};
        core.getNodeLookup().at(n.id)->setBoundingBox(newBB.x, newBB.X, newBB.y, newBB.Y);
    }

    core.setNeedNewRectangles(true);
    core.updateColaGraphRep();


}


static void seedCoreRadially(Graph& core)
{
    const auto& byId = core.getNodeLookup();
    if (byId.empty()) return;

    struct BB { id_type id; BoundingBox bb; double cx, cy; };
    std::vector<BB> nodes;
    nodes.reserve(byId.size());

    for (const auto& [id, node] : byId) {
        BoundingBox r = node->getBoundingBox();
        double cx = 0.5 * (r.x + r.X);
        double cy = 0.5 * (r.y + r.Y);
        nodes.push_back({id, r, cx, cy});
    }

    const size_t n = nodes.size();
    if (n == 0) return;

    const double meanS = core.computeAvgNodeDim();
    const double R = std::max(100.0, 2.0 * meanS * std::sqrt(double(n)));
    const double PI = 3.14159265358979323846;

    // Compute global centre of current layout
    double gx = 0.0, gy = 0.0;
    for (const auto& n : nodes) { gx += n.cx; gy += n.cy; }
    gx /= n; gy /= n;

    // Position nodes on a circle (radial)
    for (size_t k = 0; k < n; ++k) {
        const double theta = (2.0 * PI * k) / double(n);
        const double cxNew = gx + R * std::cos(theta);
        const double cyNew = gy + R * std::sin(theta);

        double halfW = 0.5 * (nodes[k].bb.X - nodes[k].bb.x);
        double halfH = 0.5 * (nodes[k].bb.Y - nodes[k].bb.y);

        BoundingBox newBB;
        newBB.x = cxNew - halfW;
        newBB.X = cxNew + halfW;
        newBB.y = cyNew - halfH;
        newBB.Y = cyNew + halfH;

        core.getNodeLookup().at(nodes[k].id)->setBoundingBox(newBB.x, newBB.X, newBB.y, newBB.Y);
    }

    //core.setNeedNewRectangles(true);
    core.updateColaGraphRep();
}

static void fdScatterLoopForTargetAR(Graph& core, unsigned int ln,
                                     double tolFrac   = 0.10,   // accept if within ±10%
                                     int    maxTries  = 20,
                                     uint32_t rngSeed = 12345U)//fixed seed for reducability
{


    const auto& byId = core.getNodeLookup();
    if (byId.empty()) return;

    double computeAR_now = computeAspectRatio(core);

    // save the current positions of all nodes
    struct Snap { id_type id; double x, y; };
    auto snapshot = [&]() {
        std::vector<Snap> s; s.reserve(core.getNodeLookup().size());
        for (const auto& [id, node] : core.getNodeLookup()) {
            BoundingBox bb = node->getBoundingBox();
            s.push_back({id, 0.5 * (bb.x + bb.X), 0.5 * (bb.y + bb.Y)});
        }
        return s;
    };

    //put nodes back to the saved positions.
    //we need this because some random tries will be worse
    //at the end we want to restore the best one we saw.
    auto restore  = [&](const std::vector<Snap>& s) {
        for (const auto& p : s) {
            auto it = core.getNodeLookup().find(p.id);
            if (it != core.getNodeLookup().end()) it->second->setCentre(p.x, p.y);
        }
    };

    // Baseline best = current layout (before any scatter)
    std::vector<Snap> bestSnap = snapshot(); // start with current layout as best
    //compute how far the current AR is from the target
    double bestErr = std::abs(computeAR_now - GLOBAL_ASPECT_RATIO) / GLOBAL_ASPECT_RATIO;

    const double lo = (1.0 - tolFrac) * GLOBAL_ASPECT_RATIO;
    const double hi = (1.0 + tolFrac) * GLOBAL_ASPECT_RATIO;


    std::mt19937 rng(rngSeed);
    std::uniform_real_distribution<double> U(-0.5, 0.5); // uniform on [-0.5, +0.5]
    int tries = 0;

    // If already within tolerance, nothing to do
    double ar = computeAR_now;
    BoundingBox B = core.getBoundingBox();
    const double cx = B.centre().x;
    const double cy = B.centre().y;
    const double W  = (B.X - B.x);
    const double H  = (B.Y - B.y);

    while ((ar < lo || ar > hi) && tries < maxTries) {

        double scatterScale = std::sqrt(byId.size()) * 10.0; //

        for (const auto& [id, node] : byId) {
            double x =   U(rng);
            double y =  U(rng);
            node->setCentre(x, y);
        }



        //save the stress before
        double stressBaseline = finalStress;

        // Free FD polish
        core.destress();

        // Evaluate and remember best
        //ar = computeAR_now;
        ar = computeAspectRatio(core);
        double errAR = std::abs(ar - GLOBAL_ASPECT_RATIO) / GLOBAL_ASPECT_RATIO;
        double currStress = finalStress;
        //std::cout<<"stressBaseline " <<stressBaseline << "currStress " <<currStress <<std::endl;
        double stressNorm = currStress / (1.0 + stressBaseline); // avoid div/0, or normalize
        double alpha =0.5; // trade off aspect ratio error vs stress
        double score = alpha * errAR + (1.0 - alpha) * stressNorm;
        //printAspectRatio(core, "core_seed_");
        //std::cout <<"err " << errAR << "  bestErr "<<bestErr<< std::endl;
        if (score < bestErr) {
            bestErr = errAR;
            bestSnap = snapshot();
        }

        ++tries;
    }

    // Restore the best attempt
    restore(bestSnap);

}
// static void seedCoreLinear(Graph& core, bool horizontal = true)
// {
//     const auto& byId = core.getNodeLookup();
//     if (byId.empty()) return;
//
//     const size_t n = byId.size();
//     if (n == 0) return;
//
//     double spacing = core.computeAvgNodeDim() * 3.0;
//     double start = -0.5 * spacing * (n - 1);
//
//     // Compute the global center so the line passes through it
//     BoundingBox B = core.getBoundingBox();
//     double cx = 0.5 * (B.x + B.X);
//     double cy = 0.5 * (B.y + B.Y);
//
//     size_t i = 0;
//     for (const auto& [id, node] : byId) {
//         double nx = horizontal ? (cx + start + i * spacing) : cx;
//         double ny = horizontal ? cy : (cy + start + i * spacing);
//         node->setCentre(nx, ny);
//         i++;
//     }
//
//     core.setNeedNewRectangles(true);
//     core.updateColaGraphRep();
// }

double computeDensity(int numNodes, int numEdges) {
    if (numNodes <= 1) return 0.0;
    return static_cast<double> (numNodes) /static_cast<double> (numEdges);
    //return (2.0 * numEdges) / (numNodes * (numNodes - 1));
}

void dialect::doHOLA(Graph &G, const HolaOpts &holaOpts, Logger *logger) {
    struct Snap {
        id_type id;
        double x, y;
    };
    // Save current positions of all nodes
    auto snapshot = [&](Graph G) {
        std::vector<Snap> s;
        s.reserve(G.getNodeLookup().size());
        for (const auto& [id, node] : G.getNodeLookup()) {
            BoundingBox bb = node->getBoundingBox();
            s.push_back({id, 0.5 * (bb.x + bb.X), 0.5 * (bb.y + bb.Y)});
        }
        return s;
    };

    // Restore saved positions
    auto restore = [&](Graph& G, const std::vector<Snap>& s) {
        for (const auto& p : s) {
            auto it = G.getNodeLookup().find(p.id);
            if (it != G.getNodeLookup().end()) {
                it->second->setCentre(p.x, p.y);
            }
        }
    };

    auto overallStart = std::chrono::high_resolution_clock::now();
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
    double IEL = G.getIEL();
    // Pad nodes
    double nodePadding = holaOpts.nodePaddingScalar*IEL;
    G.padAllNodes(nodePadding, nodePadding);

    // We need to dismantle the graph, so we begin by making a copy and we work on that instead.
    // We allocate this copy on the heap, and manage it with a shared ptr, since many of our tools
    // require that.
    Graph_SP Gcopy = std::make_shared<Graph>(G);
    // Clear any existing connector routes, for better logging output.
    Gcopy->clearAllRoutes();
// double graphDensity = computeDensity(Gcopy->getNumNodes(), Gcopy->getNumEdges());
//     std:: cout<< "graphDensity " << graphDensity << std::endl;

    // Peel.
    Trees trees = peel(*Gcopy);
    //save a copy of the node positions for the iterations later of the FD with many ARs
    std::vector<Snap> baselineCore = snapshot(*Gcopy);

    // After peeling, the input graph is peeled down to its own core.
    // Ac-cor-dingly : ) we rename it...
    Graph_SP &core = Gcopy;


    log(*core, string_format("%02d_core", ln++));

//scale core to the target AR, not sure if this helps
    //scaleCoreToTargetAR(*core);

    // log(*core, string_format("%02d_core_scaled", ln++));
    // printAspectRatio(*core, "core_scaled");

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
    }

    // Otherwise we do have a core and trees.



    core->destress();

    log(*core, string_format("%02d_free_destress_core", ln++));
    //printAspectRatio(*core, "free_destress_core");


    double freeDestressAR = computeAspectRatio(*core);
    //
    //reshufle the nodes if the AR is not reached
    // fdScatterLoopForTargetAR(*core,ln);
    //printAspectRatio(*core, "free_destress_core");
    log(*core, string_format("%02d_free_destress_core", ln++));
    freeDestressAR = computeAspectRatio(*core);

    // int retries =0 ;
    // int maxRetries =6;
    //************* change the layout of the core from random to radial
    //if (false||freeDestressAR < 0.9 * GLOBAL_ASPECT_RATIO || freeDestressAR > 1.1 * GLOBAL_ASPECT_RATIO)
    // if(false)
    // {
    //     seedCoreRadially(*core);
    //     log(*core, "core_seed_radial");
    //     printAspectRatio(*core, "core_seed_radial");
    //     auto t0 = timeNow();
    //     core->destress();
    //     auto t1 = timeNow();
    //     log(*core, string_format("%02d_free_destress_core", ln++));
    //     printAspectRatio(*core, "free_destress_core");
    //     printDuration("Free destress core", t0, t1);
    // }
    //Now destress again, this time removing any node overlaps.



    ColaOptions colaOpts;
    colaOpts.preventOverlaps = true;

    core->destress(colaOpts);


    //double corrected_AR;
    double AR_Hold = GLOBAL_ASPECT_RATIO;
    //printAspectRatio(*core, "OP_destress_core");
    double OPDestressAR = computeAspectRatio(*core);
    log(*core, string_format("%02d_OP_destress_core", ln++));
    //return;
    // double tolerance = 0.10;
    // int maxTries = 5;
    // int tries = 0;
    //
    // while (tries < maxTries) {
    //     // retrieve the baseline positions of the core
    //     //std::vector<Snap> testCore = baselineCore;
    //     restore(*core, baselineCore);
    //     core->setNeedNewRectangles(true);
    //     core->updateColaGraphRep();
    //     core->updateNodesFromRects();
    //
    //     //display to make sure it is the original
    //     log(*core, string_format("%02d_itr", ln++));
    //
    //     // Run FD layout
    //     colaOpts.preventOverlaps = false;
    //     core->destress(colaOpts);
    //     fdScatterLoopForTargetAR(*core,ln);
    //     printAspectRatio(*core, "f_destress_core");
    //     colaOpts.preventOverlaps = true;
    //     core->destress(colaOpts);
    //     printAspectRatio(*core, "OP2_destress_core");
    //
    //     //compute the AR
    //     double AR_actual = computeAspectRatio(*core);
    //     log(*core, string_format("%02d_itr1", ln++));
    //     double errorRatio = AR_actual / AR_Hold;
    //     double diff = AR_actual - AR_Hold;
    //     double relError = std::abs(diff) / AR_Hold;
    //
    //     if (std::abs(AR_actual - AR_Hold)  < tolerance) {
    //
    //         break;
    //     }
    //
    //
    //     // Adjust GLOBAL_ASPECT_RATIO
    //     if (diff > 0.0) {
    //         corrected_AR = GLOBAL_ASPECT_RATIO - 0.1 ;
    //     }
    //     else {
    //         corrected_AR = GLOBAL_ASPECT_RATIO +0.1;
    //     }
    //
    //
    //     GLOBAL_ASPECT_RATIO = corrected_AR;
    //
    //     std::cout <<"AR_actual: "<<AR_actual <<" corrected_AR "<<corrected_AR<<"GLOBAL_ASPECT_RATIO " <<GLOBAL_ASPECT_RATIO <<std::endl;
    //     tries++;
    // }


//     int tries = 0;
// double corrected_AR = GLOBAL_ASPECT_RATIO;
// double prevAR = 0.0;
// double prevGlobalAR = GLOBAL_ASPECT_RATIO;

// feedback parameters
// double k0 = 0.6;      // initial feedback gain (controls how aggressively AR is corrected)
// double decay = 0.85;  // exponential damping of gain per iteration
// double tolerance = 0.10; // acceptable relative AR error (5%)
// int maxTries = 25;    // safety cap

// while (tries < maxTries) {
//
//     // restore or reuse the layout baseline
//     restore(*core, baselineCore);
//     core->setNeedNewRectangles(true);
//     core->updateColaGraphRep();
//     core->updateNodesFromRects();
//
//     log(*core, string_format("%02d_itr", ln++));
//
//
//     colaOpts.preventOverlaps = false;
//     core->destress();
//     fdScatterLoopForTargetAR(*core, ln);
//
//     colaOpts.preventOverlaps = true;
//     core->destress(colaOpts);
//     printAspectRatio(*core, "OP2_destress_core");
//
//     // measure achieved aspect ratio
//     double AR_actual = computeAspectRatio(*core);
//     double arError = AR_actual - AR_Hold;
//     double relError = std::abs(arError) / AR_Hold;
//
//     std::cout << std::fixed << std::setprecision(3)
//               << "[AR loop] iter=" << tries
//               << "  target=" << AR_Hold
//               << "  actual=" << AR_actual
//               << "  relErr=" << relError;
//
//     // convergence check
//     if (relError < tolerance) {
//         std::cout << "   converged\n";
//         break;
//     }
//
//     //compute adaptive feedback gain
//     double kBase = k0 * std::pow(decay, tries);           // exponentially decreasing base
//     double kErr  = std::clamp(0.3 + 0.4 * relError, 0.3, 0.8); // stronger when error large
//     double k = std::clamp(kBase * kErr, 0.2, 0.8);        // combined gain, bounded
//
//     // optional sensitivity damping (if previous step exists)
//     if (tries > 0) {
//         double sens = (AR_actual - prevAR) /
//                       (GLOBAL_ASPECT_RATIO - prevGlobalAR + 1e-9);
//         double sensFactor = 1.0 / (std::abs(sens) + 1.0);
//         k *= sensFactor; // downscale if system too sensitive
//     }
//
//     // proportional feedback update
//     prevGlobalAR = GLOBAL_ASPECT_RATIO;
//     GLOBAL_ASPECT_RATIO -= k * arError;  // main correction
//     prevAR = AR_actual;
//
//     std::cout << "  k=" << k
//               << "  newGlobalAR=" << GLOBAL_ASPECT_RATIO << std::endl;
//
//     tries++;
//     std::cout <<"AR_actual: "<<AR_actual <<" corrected_AR "<<corrected_AR<<"GLOBAL_ASPECT_RATIO " <<GLOBAL_ASPECT_RATIO <<std::endl;
//     tries++;
// }

    //scale the result if it is not within AR
    // scaleCoreToTargetAR(*core);
    log(*core, string_format("%02d_core_scaled", ln++));
    //printAspectRatio(*core, "core_scaled");
    //GLOBAL_ASPECT_RATIO = AR_Hold;
    double scaledOPDestressAR = computeAspectRatio(*core);


    //destress rounds with the previous failed layout is not good
    //it is very hard for the nodes after settlement to change their positions.
    // while ((OPDestressAR < 0.9 * GLOBAL_ASPECT_RATIO || OPDestressAR > 1.1 * GLOBAL_ASPECT_RATIO)&& retries < maxRetries) {
    //     auto t0 = timeNow();
    //     core->destress();
    //     auto t1 = timeNow();
    //     log(*core, string_format("%02d_op_destress_core2", ln++));
    //     printAspectRatio(*core, "op_destress_core2");
    //     printDuration("op destress core2", t0, t1);
    //     OPDestressAR = computeAspectRatio(*core);
    //     //std::cout<<"CORE node number after "<<core->getNumNodes()<<std::endl;
    //     //**********************
    //     retries ++;
    // }


    //ARlogFile << "v" << G.getNumNodes() << "e" << G.getNumEdges()<< "," << GLOBAL_ASPECT_RATIO << "," << freeDestressAR << "," << OPDestressAR << ","<<scaledOPDestressAR<< "\n";



    // Layout the hubs. nodes with degree 3 or higher
    nli(ln);
    OrthoHubLayoutOptions ohlOpts;
    ohlOpts.avoidFlatTriangles = holaOpts.orthoHubAvoidFlatTriangles;
    OrthoHubLayout ohl(core, ohlOpts);

    ohl.layout(logger);


    log(*core, string_format("%02d_core_ortho_hub", ln++));
    //printAspectRatio(*core, "core_ortho_hub");

    double coreOrtoHubAR = computeAspectRatio(*core);


    // Set extra gap for boundary constraints.
    core->getSepMatrix().setExtraBdryGap(IEL/2.0);

    // Dissipate any stress accumulated during ortho hub layout, aiming to regain a natrual
    // distribution for the nodes that remain unconstrained, and perhaps regain natural symmetries.
    // This time, besides just preventing overlaps between nodes, we also prevent any nodes from
    // overlapping with aligned edges.

    //the idea is to insert invisible “edgenodes” along these aligned edges and treat them like real nodes
    //adding constraints to force space between them and the original nodes.
    //Z: here I think we should either shut down the biased distances or adapt it to the AR.
    colaOpts.solidifyAlignedEdges = true;
    colaOpts.logger = logger;
    nli(ln);


    core->destress(colaOpts);


    log(*core, string_format("%02d_EOP_destress_core", ln++));
    //printAspectRatio(*core, "EOP_destress_core");

    // Next we lay out the links.
    // We may or may not build Chains for this process. Later we will need to know whether chains
    // were built, so the vector of Chains is declared at this scope.
    double EOP_destress_core = computeAspectRatio(*core);




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
        for (Chain_SP chain : chains) chain->takeShapeBasedConfiguration();
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
        //printAspectRatio(*core, "core_link_config_ACA");

    } else {
        log(*core, string_format("%02d_core_link_config_Chains", ln++));
    }

    //scaleCoreToTargetAR(*core);

    // log(*core, string_format("%02d_core_scaled", ln++));
    // printAspectRatio(*core, "core_scaled");

    double core_link_config_ACA = computeAspectRatio(*core);
    // ARlogFile << "v" << G.getNumNodes() << "e" << G.getNumEdges()<< "," << GLOBAL_ASPECT_RATIO
    // << "," << freeDestressAR << "," << OPDestressAR << "," << coreOrtoHubAR <<","<<EOP_destress_core<<","
    // << core_link_config_ACA
    // <<"\n";

    //return;
    // Next is the phase in which we planarise the core.
    // However, we want a 4-planar orthogonal layout with no leaves for this phase, so we first
    // perform a special orthogonal connector routing, which ensures that no nodes will become
    // leaves in the planarisation. (It does this by ensuring that connectors are routed to at
    // least two distinct sides of each node.)
    LeaflessOrthoRouter lor(core, holaOpts);
    nli(ln);

    lor.route(logger);

    ++ln;

    log(*core, string_format("%02d_core_leafless_ortho_route", ln++));
    //printAspectRatio(*core, "core_leafless_ortho_route");

    double core_leafless_ortho = computeAspectRatio(G);
    //create dummy nodes for every bend
    OrthoPlanariser op(core);

    Graph_SP P = op.planarise();


    log(*P, string_format("%02d_planar_graph_P", ln++));
    //printAspectRatio(*P, "planar_graph_P");

    auto before = G.computeEdgeLengths();
    // Set extra gap for boundary constraints.
    P->getSepMatrix().setExtraBdryGap(IEL/2.0);

    // Destress the new planar graph P, aiming to regain possible natural symmetries.
    // But use overlap prevention so that the structure cannot change.
    // (Note that now /all/ edges are aligned, so we have total edge-node overlap prevention.)



    colaOpts.preventOverlaps = true;
    colaOpts.solidifyAlignedEdges = true;
    nli(ln);

    P->destress(colaOpts);


    log(*P, string_format("%02d_P_EOP_destress", ln++));
    //printAspectRatio(*P, "P_EOP_destress");

    double P_EOP_destress = computeAspectRatio(*P);
    //auto after = G.computeEdgeLengths();

    //debug the edges that changed
    // for (auto &entry : before) {
    //     auto sid = entry.first.first;
    //     auto tid = entry.first.second;
    //
    //     double oldLen = entry.second;
    //     double newLen = after[{sid, tid}];
    //
    //     std::cout << "Edge " << sid << "-" << tid
    //               << " old=" << oldLen
    //               << " new=" << newLen
    //               << " change=" << (newLen - oldLen)
    //               << std::endl;
    // }

//GLOBAL_ASPECT_RATIO = AR_Hold;
    // Now we want to reattach the trees, choosing faces of the planarised core in which to
    // place them.
    // First the trees need their own symmetric layout.

    unsigned lns = 0;  // initialise logging sub-index
    for (Tree_SP tree : trees) {
        tree->symmetricLayout(
            holaOpts.defaultTreeGrowthDir,
            holaOpts.treeLayoutScalar_nodeSep*IEL,
            holaOpts.treeLayoutScalar_rankSep*IEL,
            holaOpts.preferConvexTrees
        );
        //log(*(tree->underlyingGraph()), string_format("%02d_%02d_symm_tree", ln, lns++));
    }

    ++ln;
    nli(ln);



    // Now we can choose faces and reattach them.
    FaceSet_SP faceSet = reattachTrees(P, trees, holaOpts, logger);
    ++ln;
    // We will need the vector of chosen tree placements.
    TreePlacements tps = faceSet->getAllTreePlacements();


    // Next we insert the actual trees back into the planar graph.
    // The trees come with buffer nodes. We build a record of those, so they can be
    // ignored where necessary.
    NodesById bufferNodes;
    EdgesById treeEdges;
    std::vector<NodesById> clustersSansBufferNodes;
    for (auto tp : tps) {
        tp->applyGeometryToTree();
        NodesById treeNodes;
        NodesById buffNodes;
        tp->insertTreeIntoGraph(*P, treeNodes, buffNodes, treeEdges);
        clustersSansBufferNodes.push_back(treeNodes);
        treeNodes.insert(buffNodes.begin(), buffNodes.end());
        bufferNodes.insert(buffNodes.begin(), buffNodes.end());
        colaOpts.nodeClusters.push_back(treeNodes);
    }

    log(*P, string_format("%02d_P_with_trees", ln++));
    //printAspectRatio(*P, "P_with_trees");


    double with_trees = computeAspectRatio(*P);
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

    //GLOBAL_ASPECT_RATIO = corrected_AR;
    //std::cout<< "GLOBAL_ASPECT_RATIO "<< GLOBAL_ASPECT_RATIO <<std::endl;
    nli(ln);

    P->destress(colaOpts);

    log(*P, string_format("%02d_P_nbr_destress", ln++));
    //printAspectRatio(*P, "P_nbr_destress");




    double Pnbr = computeAspectRatio(*P);
    //Do near alignments.
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

        //printAspectRatio(*P, "P_near_alignments");

    }

    double near_alignments = computeAspectRatio(*P);
    //scale a bit
    scaleCoreToTargetAR(*P);
    double near_AR = computeAspectRatio(*P);
    log(*P, string_format("%02d_P_near_AR", ln++));

    // Delete buffer nodes.
    P->removeNodes(bufferNodes);
    colaOpts.nodeClusters = clustersSansBufferNodes;

    // Rotate if desired.
    //Z: no rotation
    if (false && holaOpts.preferredAspectRatio != AspectRatioClass::NONE) {
        BoundingBox b = P->getBoundingBox(bufferNodes);
        double w = b.w(),
               h = b.h();
        bool scaleBySize = true;
        unsigned quarterTurnsCW = 0;
        auto counts = faceSet->getNumTreesByGrowthDir(scaleBySize);
        if ((w < h && holaOpts.preferredAspectRatio == AspectRatioClass::LANDSCAPE) ||
            (h < w && holaOpts.preferredAspectRatio == AspectRatioClass::PORTRAIT)) {
            std::cout<<"preferredAspectRatio "<<std::endl;
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
            std::cout<<"rotate only "<<std::endl;
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
        log(*P, string_format("%02d_P_rotation", ln++));
        printAspectRatio(*core, "P_rotation");
    }


    // Translate if desired.
    if (holaOpts.putUlcAtOrigin) {
        NodesById ignore; // leave empty; don't ignore any nodes
        bool includeBends = true; // we want the edge routes included
        BoundingBox b = P->getBoundingBox(ignore, includeBends);
        double dx = -b.x,
               dy = -b.y;
        P->translate(dx, dy);

        log(*P, string_format("%02d_P_translation", ln++));
        //printAspectRatio(*P, "P_translation");

    }


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

    // Final connector routing.
    G.clearAllRoutes();
    log(G, string_format("%02d_final", ln++));
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

    // Set up a routing adapter.
    RoutingAdapter ra(Avoid::OrthogonalRouting);
    ra.router.setRoutingOption(Avoid::nudgeOrthogonalSegmentsConnectedToShapes, true);
    ra.router.setRoutingOption(Avoid::nudgeSharedPathsWithCommonEndPoint, true);
    ra.router.setRoutingParameter(Avoid::crossingPenalty, 2*IEL);//increase it if you do not want edge crossings
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

    // Remove remaining node padding.
    G.padAllNodes(-nodePaddingLayer2, -nodePaddingLayer2);
    //printAspectRatio(G, "Final AR");




    double finalAR = computeAspectRatio(G);

    auto overallEnd = std::chrono::high_resolution_clock::now();
    auto totalTime = std::chrono::duration_cast<std::chrono::milliseconds>(overallEnd - overallStart).count();

    ARlogFile << GraphName << "," << GLOBAL_ASPECT_RATIO << "," << freeDestressAR << "," << OPDestressAR
            << "," << scaledOPDestressAR <<"," << coreOrtoHubAR <<","<<EOP_destress_core<<","<< core_link_config_ACA <<","
            << core_leafless_ortho << "," << P_EOP_destress<<","<< with_trees <<","<< Pnbr<< ","<< near_alignments<<"," <<near_AR <<","
            << finalAR <<"," << totalTime <<"\n";
    //printDuration("Total HOLA time", overallStart, overallEnd);
    //GLOBAL_ASPECT_RATIO = AR_Hold;
}
