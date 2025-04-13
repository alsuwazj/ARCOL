#include <iostream>
#include <fstream>
#include <string>
#include "libdialect/graphs.h"
#include "libdialect/hola.h"
#include "libdialect/io.h"

using namespace dialect;

int main(int argc, char *argv[]) {
    // Ensure we have input and output filenames
    if (argc != 3) {
        std::cerr << "Usage: " << argv[0] << " input.tglf output.tglf" << std::endl;
        return 1;
    }

    std::string inputFile = argv[1];
    std::string outputFile = argv[2];

    // Generate the corresponding SVG filename
    std::string svgFile = outputFile.substr(0, outputFile.find_last_of(".")) + ".svg";

    try {
        // Load the graph from a TGLF file
        std::cout << "Loading graph from: " << inputFile << std::endl;
        Graph_SP graph = buildGraphFromTglfFile(inputFile);

        // Create a logger to save intermediate steps
        Logger logger("logs/hola_step");

        std:: cout  << logger.outputDir<<std::endl;
        // Apply HOLA layout
        std::cout << "Applying HOLA layout..." << std::endl;



        doHOLA(*graph,{}, &logger);

        // Save updated graph in TGLF format
        std::cout << "Saving updated TGLF graph to: " << outputFile << std::endl;
        std::string tglfOutput = graph->writeTglf();
        writeStringToFile(tglfOutput, outputFile);

        // Save updated graph in SVG format
        std::cout << "Saving SVG graph to: " << svgFile << std::endl;
        std::string svgOutput = graph->writeSvg();
        writeStringToFile(svgOutput, svgFile);

        std::cout << "Done!" << std::endl;

    } catch (const std::exception &e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
