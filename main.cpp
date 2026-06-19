#include <iostream>
#include <fstream>
#include <string>
#include "libdialect/graphs.h"
#include "libdialect/hola.h"
#include "libdialect/io.h"
#include "libdialect/nodeconfig.h"
#include "libcola/cola_log.h"
#include "libdialect/logging.h"
#include "libdialect/metrics.h"
#include <filesystem>
#include <regex>
#include <unordered_set>

using namespace dialect;
//using dialect::GraphName;
namespace fs = std::filesystem;

void summarizeAspectRatioLog(const std::string& inputFilePath, const std::string& summaryFilePath) {
    std::ifstream infile(inputFilePath);
    if (!infile.is_open()) {
        std::cerr << "Error opening input file: " << inputFilePath << "\n";
        return;
    }

    std::ofstream summary(summaryFilePath);
    if (!summary.is_open()) {
        std::cerr << "Error opening summary file: " << summaryFilePath << "\n";
        return;
    }

    std::string line;
    int total = 0, free_ok = 0, op_ok = 0, both_ok = 0;

    std::getline(infile, line); // skip header

    while (std::getline(infile, line)) {
        total++;
        std::stringstream ss(line);
        std::string graphName, cell;
        double targetAR = 0.0, freeAR = 0.0, opAR = 0.0;

        // Parse columns safely
        if (!std::getline(ss, graphName, ',')) continue;
        if (!std::getline(ss, cell, ',')) continue;
        try { targetAR = std::stod(cell); } catch (...) { continue; }

        if (!std::getline(ss, cell, ',')) continue;
        try { freeAR = std::stod(cell); } catch (...) { continue; }

        if (!std::getline(ss, cell, ',')) continue;
        try { opAR = std::stod(cell); } catch (...) { continue; }



        bool freeWithin = (freeAR >= 0.8 * targetAR && freeAR <= 1.2 * targetAR);
        bool opWithin   = (opAR   >= 0.8 * targetAR && opAR   <= 1.2 * targetAR);

        if (freeWithin) free_ok++;
        if (opWithin)   op_ok++;
        if (freeWithin && opWithin) both_ok++;
    }

    // Write to summary file
    summary << "Graph AR Achievement Summary:\n";
    summary << "  Total graphs:        " << total << "\n";
    summary << "  Free Destress OK:    " << free_ok << "\n";
    summary << "  Op Destress OK:      " << op_ok << "\n";
    summary << "  Both OK:             " << both_ok << "\n";

    // // Optional: also print to console
    // std::cout << "Aspect Ratio Summary:\n";
    // std::cout << "  Total graphs:        " << total << "\n";
    // std::cout << "  Free Destress OK:    " << free_ok << "\n";
    // std::cout << "  Op Destress OK:      " << op_ok << "\n";
    // std::cout << "  Both OK:             " << both_ok << "\n";
}
int main(int argc, char *argv[]) {
    // cola::Output2FILE::Stream() = stderr;
    // cola::FILELog::ReportingLevel() = cola::logDEBUG4;
    // FILE_LOG(cola::logDEBUG) << "Logging initialized in libcola.";

    ARlogFile << "Graph Name,Target AR,Free Destress,Op Destress, scaled Op Destress, core Ortho Hub, EOP_destress_core,core_link_config_ACA, "
                 "core leafless ortho, P_EOP destress, with trees, Pnbr, near alignment, near_AR_scaling, final, time  \n";

    std::string inputArg = argv[1];

    // Validate input path
    if (!fs::exists(inputArg)) {
        std::cerr << "Error: " << inputArg << " does not exist." << std::endl;
        return 1;
    }
    bool isFile = fs::is_regular_file(inputArg);
    bool isFolder = fs::is_directory(inputArg);

    if (!isFile && !isFolder) {
        std::cerr << "Error: " << inputArg << " is not a valid file or folder." << std::endl;
        return 1;
    }

    if (argc < 2) {
        std::cerr << "Usage:\n"
                  << "  Single graph: " << argv[0] << " input.tglf output.tglf [aspect_ratio]\n"
                  << "  Batch folder: " << argv[0] << " folder_path [aspect_ratio]" << std::endl;
        return 1;
    }


    if (isFile && argc == 4) {
        GLOBAL_ASPECT_RATIO = std::stod(argv[3]);
        std::cout << "Aspect ratio set for single file: " << GLOBAL_ASPECT_RATIO << std::endl;
    }
    else if (isFolder && argc == 4) {
        GLOBAL_ASPECT_RATIO = std::stod(argv[3]);
        std::cout << "Aspect ratio set for folder: " << GLOBAL_ASPECT_RATIO << std::endl;
    }
    if (isFile) {
        // --- Single Graph Mode ---
        std::string inputFile = argv[1];
        std::string outputFile = argv[2];
        std::string svgFile = outputFile.substr(0, outputFile.find_last_of(".")) + ".svg";

        std::string fileStem = std::filesystem::path(inputFile).stem().string();
        std::string logDir = "logs/" + fileStem;
        std::filesystem::create_directories(logDir);

        GraphName = fileStem;
        Logger logger(logDir);

        try {
            std::cout << "Loading graph from: " << inputFile << std::endl;
            Graph_SP graph = buildGraphFromTglfFile(inputFile);

            std::cout << "Applying HOLA layout..." << std::endl;
            doHOLA(*graph, {}, &logger);

            writeStringToFile(graph->writeTglf(), outputFile);
            writeStringToFile(graph->writeSvg(), svgFile);

            std::cout << "Done!" << std::endl;
        }
        catch (const std::exception &e) {
            std::cerr << "Error: " << e.what() << std::endl;
            return 1;
        }

    } else if (isFolder) {
        //std::string metricsFile = "results/summary_metrics.csv";
        //std::filesystem::create_directories("results");


        // logMetrics << "graph,nodes,edges,density,maxDegree,targetAR,finalAR,AR_error,stress,"
        //                 << "edgeCross,edgeLenDev,nodeRes,nodeUni,runtime \n";



        auto folderTotalStart = std::chrono::high_resolution_clock::now();
        // --- Batch Folder Mode ---
        //16:9 (Widescreen) 1.7
        //9:16(Vertical) mobile 0.5
        //1:1 social media platforms like Instagram
        //21:9 (Cinematic Widescreen) 2.3
        //4:3 ipad 1.3
        // 1:1.414 A4 0.7
        //9.0/16.0, 1.0, 4.0/3.0, 16.0/9.0, 21.0/9
        //Extremely wide panoramic 32:9 3.55
        //very tall 1 : 3 0.33
        //48:9 ≈ 5.33 triple ultrawide wall displays
        //1.0/3.0, 9.0/16.0, 1.0, 4.0/3.0, 16.0/9.0, 21.0/9, 32.0/9.0
        std::vector<double> aspectRatios = {  0.5, 0.6, 0.7, 0.8, 0.9, 1.0, 1.1, 1.2, 1.3, 1.4, 1.5, 1.6, 1.7, 1.8, 1.9, 2.0};

        std::string outputFolder = argv[2];
        std::filesystem::create_directories(outputFolder);  //

        for (double ar : aspectRatios) {
            std::cout << "\n=== Running batch with Aspect Ratio: " << ar << " ===\n";
            GLOBAL_ASPECT_RATIO = ar;


            if (!ARlogFile.is_open()) {
                std::cerr << "Failed to open log file!" << std::endl;
                return 1;
            }
            if (!logMetrics.is_open()) {
                std::cerr << "Failed to open log metrics file!" << std::endl;
                return 1;
            }



            auto arTag = [](double v) {
                std::ostringstream os;
                os << std::fixed << std::setprecision(2) << v;
                std::string s = os.str();
                std::replace(s.begin(), s.end(), '.', 'p'); // 1.30 -> 1p30
                return s;
            };

            std::string outputSubfolder = std::string(argv[2]) + "_AR" + arTag(ar);
            std::filesystem::create_directories(outputSubfolder);


            // Step 1: Collect and sort the entries
            std::vector<fs::directory_entry> entries;

            for (const auto& entry : fs::directory_iterator(inputArg)) {
                if (entry.is_regular_file() && entry.path().extension() == ".tglf") {
                    entries.push_back(entry);
                }
            }

            // auto custom_sort = [](const fs::directory_entry& a, const fs::directory_entry& b) {
            //     std::regex ve_regex("v(\\d+)e(\\d+)");
            //     std::smatch match_a, match_b;
            //     std::string a_str = a.path().stem().string();
            //     std::string b_str = b.path().stem().string();
            //
            //     if (std::regex_match(a_str, match_a, ve_regex) &&
            //         std::regex_match(b_str, match_b, ve_regex)) {
            //         int v_a = std::stoi(match_a[1]);
            //         int e_a = std::stoi(match_a[2]);
            //         int v_b = std::stoi(match_b[1]);
            //         int e_b = std::stoi(match_b[2]);
            //
            //         if (v_a == v_b)
            //             return e_a < e_b;
            //         return v_a < v_b;
            //         }
            //     return a_str < b_str; // fallback
            // };
            //
            // std::sort(entries.begin(), entries.end(), custom_sort);

            std::unordered_set<std::string> keep;
            std::ifstream fin("Rome_selected_graphs.txt");
            std::string name;
            while (std::getline(fin, name)) keep.insert(name);
            fin.close();

            std::vector<fs::directory_entry> selectedEntries;
            for (const auto& e : fs::directory_iterator(inputArg)) {
                if (keep.count(e.path().filename().string()))
                    selectedEntries.push_back(e);
            }

            // Step 3: Process every 7th file starting from the first
            int index = 0;
            for (const auto& entry : entries) {
                if (true ){//index >= 25 && index <= 85 && (index - 30) % 5 == 0 ) { //index % 24 == 0
                    try {
                        std::string inputFile = entry.path().string();
                        std::string fileStem = entry.path().stem().string();
                        std::string outputFile = outputSubfolder + "/" + fileStem + "_AR" + arTag(ar) + ".tglf";
                        std::string svgFile    = outputSubfolder + "/" + fileStem + "_AR" + arTag(ar) + ".svg";
                        std::string logDir = "logs/" + fileStem + "_AR" + std::to_string(ar);

                        std::filesystem::create_directories(logDir);


                        std::cout << "Processing: " << inputFile << std::endl;
                        Graph_SP graph = buildGraphFromTglfFile(inputFile);
                        auto startTime = std::chrono::high_resolution_clock::now();
                        std::cout << "Running HOLA on: " << fileStem << "\n";
                        Logger logger(logDir);
                        dialect::GraphName = fileStem;

                        doHOLA(*graph, {}, &logger);
                        std::cout << "Finished HOLA on: " << fileStem << "\n";


                        auto endTime = std::chrono::high_resolution_clock::now();
                        // LayoutMetrics metrics(*graph, ar);
                        // double startMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                        //                     startTime.time_since_epoch()).count();
                        // double endMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                        //                    endTime.time_since_epoch()).count();
                        // metrics.computeMetrics ( *graph, startMs,  endMs) ;
                        // std::cout << "[DEBUG] Metrics computed" << std::endl;
                        // metrics.printSummary();
                        // metrics.writeCSV();
                        // //
                        // std::cout << "Metrics logged for: " << fileStem << std::endl;
                        writeStringToFile(graph->writeTglf(true), outputFile);
                        writeStringToFile(graph->writeSvg(true), svgFile);

                        std::cout << "Done with: " << fileStem << std::endl;



                        dialect::resetNodeAndEdgeIDs();
                        graph.reset();

                    } catch (const std::exception& e) {
                        std::cerr << "Error processing " << entry.path() << ": " << e.what() << std::endl;
                    }
                }
                index++;
            }
        }

        //summarizeAspectRatioLog("aspect_ratio_log.csv", "AR_summary.txt");

        ARlogFile.close();
        auto folderTotalEnd = std::chrono::high_resolution_clock::now();
        auto totalDuration = std::chrono::duration_cast<std::chrono::milliseconds>(folderTotalEnd - folderTotalStart).count();

        std::cout << "\n======================" << std::endl;
        std::cout << "Total runtime: " << totalDuration / 1000.0 << " seconds" << std::endl;
        std::cout << "======================" << std::endl;
        logMetrics.close();

    }


std::cout<<"last "<<std::endl;
    return 0;
}

