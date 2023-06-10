#include <stdio.h>
#include <stdlib.h>
#include <iostream>
#include <set>
#include "vg/io/json2pb.h"
#include "../vg.hpp"
#include "catch.hpp"
#include "bdsg/hash_graph.hpp"
#include "../integrated_snarl_finder.hpp"
#include "random_graph.hpp"
#include "../zip_code_tree.hpp"
#include <random>
#include <time.h>

//#define print

namespace vg {
namespace unittest {

    TEST_CASE( "zip tree one node",
                   "[zip_tree]" ) {
        VG graph;

        Node* n1 = graph.create_node("GCA");


        IntegratedSnarlFinder snarl_finder(graph);
        SnarlDistanceIndex distance_index;
        fill_in_distance_index(&distance_index, &graph, &snarl_finder);
        SnarlDistanceIndexClusterer clusterer(distance_index, &graph);
        
        //graph.to_dot(cerr);

        SECTION( "One seed" ) {
 
            id_t seed_nodes[] = {1};
            //all are in the same cluster
            vector<SnarlDistanceIndexClusterer::Seed> seeds;
            for (id_t n : seed_nodes) {
                pos_t pos = make_pos_t(n, false, 0);
                ZipCode zipcode;
                zipcode.fill_in_zipcode(distance_index, pos);
                seeds.push_back({ pos, 0, zipcode});
            }

            ZipCodeTree zip_tree(seeds, distance_index);
            zip_tree.print_self();

            REQUIRE(zip_tree.get_tree_size() == 3);
            REQUIRE(zip_tree.get_item_at_index(0).type == ZipCodeTree::CHAIN_START);
            REQUIRE(zip_tree.get_item_at_index(1).type == ZipCodeTree::SEED);
            REQUIRE(zip_tree.get_item_at_index(1).value == 0);
            REQUIRE(zip_tree.get_item_at_index(2).type == ZipCodeTree::CHAIN_END);
        }

        SECTION( "Two seeds" ) {
 
            id_t seed_nodes[] = {1, 1};
            //all are in the same cluster
            vector<SnarlDistanceIndexClusterer::Seed> seeds;
            for (id_t n : seed_nodes) {
                pos_t pos = make_pos_t(n, false, 0);
                ZipCode zipcode;
                zipcode.fill_in_zipcode(distance_index, pos);
                seeds.push_back({ pos, 0, zipcode});
            }

            ZipCodeTree zip_tree(seeds, distance_index);
            zip_tree.print_self();

            REQUIRE(zip_tree.get_tree_size() == 5);


            //Chain start
            REQUIRE(zip_tree.get_item_at_index(0).type == ZipCodeTree::CHAIN_START);

            //Seed (either one because they're the same position)
            REQUIRE(zip_tree.get_item_at_index(1).type == ZipCodeTree::SEED);
            REQUIRE((zip_tree.get_item_at_index(1).value == 0 ||
                     zip_tree.get_item_at_index(1).value == 1));

            //Distance between the seeds
            REQUIRE(zip_tree.get_item_at_index(2).type == ZipCodeTree::EDGE);
            REQUIRE(zip_tree.get_item_at_index(2).value == 0);

            //THe other seed
            REQUIRE(zip_tree.get_item_at_index(3).type == ZipCodeTree::SEED);
            REQUIRE((zip_tree.get_item_at_index(3).value == 0 ||
                     zip_tree.get_item_at_index(3).value == 1));

            //Chain end
            REQUIRE(zip_tree.get_item_at_index(4).type == ZipCodeTree::CHAIN_END);
        }

        SECTION( "Three seeds" ) {
 
            vector<pos_t> positions;
            positions.emplace_back(1, false, 0);
            positions.emplace_back(1, false, 0);
            positions.emplace_back(1, false, 2);
            //all are in the same cluster
            vector<SnarlDistanceIndexClusterer::Seed> seeds;
            for (pos_t pos : positions) {
                ZipCode zipcode;
                zipcode.fill_in_zipcode(distance_index, pos);
                seeds.push_back({ pos, 0, zipcode});
            }

            ZipCodeTree zip_tree(seeds, distance_index);
            zip_tree.print_self();

            REQUIRE(zip_tree.get_tree_size() == 7);


            //Chain start
            REQUIRE(zip_tree.get_item_at_index(0).type == ZipCodeTree::CHAIN_START);

            //Seed (either one because they're the same position)
            REQUIRE(zip_tree.get_item_at_index(1).type == ZipCodeTree::SEED);
            REQUIRE((zip_tree.get_item_at_index(1).value == 0 ||
                     zip_tree.get_item_at_index(1).value == 1));

            //Distance between the seeds
            REQUIRE(zip_tree.get_item_at_index(2).type == ZipCodeTree::EDGE);
            REQUIRE(zip_tree.get_item_at_index(2).value == 0);

            //THe other seed
            REQUIRE(zip_tree.get_item_at_index(3).type == ZipCodeTree::SEED);
            REQUIRE((zip_tree.get_item_at_index(3).value == 0 ||
                     zip_tree.get_item_at_index(3).value == 1));

            //Distance between the seeds
            REQUIRE(zip_tree.get_item_at_index(4).type == ZipCodeTree::EDGE);
            REQUIRE(zip_tree.get_item_at_index(4).value == 2);

            //THe other seed
            REQUIRE(zip_tree.get_item_at_index(5).type == ZipCodeTree::SEED);
            REQUIRE(zip_tree.get_item_at_index(5).value == 2);

            //Chain end
            REQUIRE(zip_tree.get_item_at_index(6).type == ZipCodeTree::CHAIN_END);
        }
    }
    TEST_CASE( "zip tree two node chain", "[zip_tree]" ) {
        VG graph;

        Node* n1 = graph.create_node("GCA");
        Node* n2 = graph.create_node("GCAAGGT");

        Edge* e1 = graph.create_edge(n1, n2);


        IntegratedSnarlFinder snarl_finder(graph);
        SnarlDistanceIndex distance_index;
        fill_in_distance_index(&distance_index, &graph, &snarl_finder);
        SnarlDistanceIndexClusterer clusterer(distance_index, &graph);
        
        //graph.to_dot(cerr);

        SECTION( "Three seeds" ) {
 
            vector<pos_t> positions;
            positions.emplace_back(1, false, 0);
            positions.emplace_back(1, false, 1);
            positions.emplace_back(2, false, 2);
            //all are in the same cluster
            vector<SnarlDistanceIndexClusterer::Seed> seeds;
            for (pos_t pos : positions) {
                ZipCode zipcode;
                zipcode.fill_in_zipcode(distance_index, pos);
                seeds.push_back({ pos, 0, zipcode});
            }

            ZipCodeTree zip_tree(seeds, distance_index);
            zip_tree.print_self();

            REQUIRE(zip_tree.get_tree_size() == 7);

            //The order should either be 0-1-2, or 2-1-0
            bool is_rev = zip_tree.get_item_at_index(1).value == 2;
            if (is_rev) {

                //Chain start
                REQUIRE(zip_tree.get_item_at_index(0).type == ZipCodeTree::CHAIN_START);

                //first seed 
                REQUIRE(zip_tree.get_item_at_index(1).type == ZipCodeTree::SEED);
                REQUIRE(zip_tree.get_item_at_index(1).value == 2);

                //Distance between the seeds
                REQUIRE(zip_tree.get_item_at_index(2).type == ZipCodeTree::EDGE);
                REQUIRE(zip_tree.get_item_at_index(2).value == 4);

                //The next seed
                REQUIRE(zip_tree.get_item_at_index(3).type == ZipCodeTree::SEED);
                REQUIRE(zip_tree.get_item_at_index(3).value == 1);

                //Distance between the seeds
                REQUIRE(zip_tree.get_item_at_index(4).type == ZipCodeTree::EDGE);
                REQUIRE(zip_tree.get_item_at_index(4).value == 1);

                //The last seed
                REQUIRE(zip_tree.get_item_at_index(5).type == ZipCodeTree::SEED);
                REQUIRE(zip_tree.get_item_at_index(5).value == 0);

                //Chain end
                REQUIRE(zip_tree.get_item_at_index(6).type == ZipCodeTree::CHAIN_END);
            } else {

                //Chain start
                REQUIRE(zip_tree.get_item_at_index(0).type == ZipCodeTree::CHAIN_START);

                //first seed 
                REQUIRE(zip_tree.get_item_at_index(1).type == ZipCodeTree::SEED);
                REQUIRE(zip_tree.get_item_at_index(1).value == 0);

                //Distance between the seeds
                REQUIRE(zip_tree.get_item_at_index(2).type == ZipCodeTree::EDGE);
                REQUIRE(zip_tree.get_item_at_index(2).value == 1);

                //The next seed
                REQUIRE(zip_tree.get_item_at_index(3).type == ZipCodeTree::SEED);
                REQUIRE(zip_tree.get_item_at_index(3).value == 1);

                //Distance between the seeds
                REQUIRE(zip_tree.get_item_at_index(4).type == ZipCodeTree::EDGE);
                REQUIRE(zip_tree.get_item_at_index(4).value == 4);

                //The last seed
                REQUIRE(zip_tree.get_item_at_index(5).type == ZipCodeTree::SEED);
                REQUIRE(zip_tree.get_item_at_index(5).value == 2);

                //Chain end
                REQUIRE(zip_tree.get_item_at_index(6).type == ZipCodeTree::CHAIN_END);
            }
        }
    }
    TEST_CASE( "zip tree two two node chains", "[zip_tree]" ) {
        VG graph;

        Node* n1 = graph.create_node("GCA");
        Node* n2 = graph.create_node("GCAAGGT");
        Node* n3 = graph.create_node("GCA");
        Node* n4 = graph.create_node("GCAAGGT");

        Edge* e1 = graph.create_edge(n1, n2);
        Edge* e2 = graph.create_edge(n3, n4);


        IntegratedSnarlFinder snarl_finder(graph);
        SnarlDistanceIndex distance_index;
        fill_in_distance_index(&distance_index, &graph, &snarl_finder);
        SnarlDistanceIndexClusterer clusterer(distance_index, &graph);
        
        //graph.to_dot(cerr);

        SECTION( "One seed on each component" ) {
 
            vector<pos_t> positions;
            positions.emplace_back(1, false, 0);
            positions.emplace_back(3, false, 0);
            //all are in the same cluster
            vector<SnarlDistanceIndexClusterer::Seed> seeds;
            for (pos_t pos : positions) {
                ZipCode zipcode;
                zipcode.fill_in_zipcode(distance_index, pos);
                seeds.push_back({ pos, 0, zipcode});
            }

            ZipCodeTree zip_tree(seeds, distance_index);
            zip_tree.print_self();

            //The tree should be:
            // [pos1] [pos3]
            REQUIRE(zip_tree.get_tree_size() == 6);

            //Chain start
            REQUIRE(zip_tree.get_item_at_index(0).type == ZipCodeTree::CHAIN_START);

            //first seed 
            REQUIRE(zip_tree.get_item_at_index(1).type == ZipCodeTree::SEED);

            //Chain end
            REQUIRE(zip_tree.get_item_at_index(2).type == ZipCodeTree::CHAIN_END);

            //Chain start
            REQUIRE(zip_tree.get_item_at_index(3).type == ZipCodeTree::CHAIN_START);

            //The first seed in the new chain
            REQUIRE(zip_tree.get_item_at_index(4).type == ZipCodeTree::SEED);

            //Chain end
            REQUIRE(zip_tree.get_item_at_index(5).type == ZipCodeTree::CHAIN_END);
        }
        SECTION( "Four seeds" ) {
 
            vector<pos_t> positions;
            positions.emplace_back(1, false, 0);
            positions.emplace_back(2, false, 2);
            positions.emplace_back(3, false, 0);
            positions.emplace_back(4, false, 2);
            //all are in the same cluster
            vector<SnarlDistanceIndexClusterer::Seed> seeds;
            for (pos_t pos : positions) {
                ZipCode zipcode;
                zipcode.fill_in_zipcode(distance_index, pos);
                seeds.push_back({ pos, 0, zipcode});
            }

            ZipCodeTree zip_tree(seeds, distance_index);
            zip_tree.print_self();

            //The tree should be:
            // [pos1 5 pos2] [pos3 5 pos4]
            // of
            // [pos2 5 pos1] [ pos3 5 pos4]
            // etc...
            REQUIRE(zip_tree.get_tree_size() == 10);

            //Chain start
            REQUIRE(zip_tree.get_item_at_index(0).type == ZipCodeTree::CHAIN_START);

            //first seed 
            REQUIRE(zip_tree.get_item_at_index(1).type == ZipCodeTree::SEED);

            //Distance between the seeds
            REQUIRE(zip_tree.get_item_at_index(2).type == ZipCodeTree::EDGE);
            REQUIRE(zip_tree.get_item_at_index(2).value == 5);

            //The next seed
            REQUIRE(zip_tree.get_item_at_index(3).type == ZipCodeTree::SEED);

            //Chain end
            REQUIRE(zip_tree.get_item_at_index(4).type == ZipCodeTree::CHAIN_END);

            //Chain start
            REQUIRE(zip_tree.get_item_at_index(5).type == ZipCodeTree::CHAIN_START);

            //The first seed in the new chain
            REQUIRE(zip_tree.get_item_at_index(6).type == ZipCodeTree::SEED);

            //Distance between the seeds
            REQUIRE(zip_tree.get_item_at_index(7).type == ZipCodeTree::EDGE);
            REQUIRE(zip_tree.get_item_at_index(7).value == 5);

            //The last seed
            REQUIRE(zip_tree.get_item_at_index(8).type == ZipCodeTree::SEED);

            //Chain end
            REQUIRE(zip_tree.get_item_at_index(9).type == ZipCodeTree::CHAIN_END);
        }
    }
    TEST_CASE( "zip tree simple bubbles in chains", "[zip_tree]" ) {
        VG graph;

        Node* n1 = graph.create_node("GCA");
        Node* n2 = graph.create_node("GCAAGGT");
        Node* n3 = graph.create_node("GCA");
        Node* n4 = graph.create_node("GCA");
        Node* n5 = graph.create_node("GCA");
        Node* n6 = graph.create_node("GCA");

        Edge* e1 = graph.create_edge(n1, n2);
        Edge* e2 = graph.create_edge(n1, n3);
        Edge* e3 = graph.create_edge(n2, n3);
        Edge* e4 = graph.create_edge(n3, n4);
        Edge* e5 = graph.create_edge(n3, n5);
        Edge* e6 = graph.create_edge(n4, n6);
        Edge* e7 = graph.create_edge(n5, n6);


        IntegratedSnarlFinder snarl_finder(graph);
        SnarlDistanceIndex distance_index;
        fill_in_distance_index(&distance_index, &graph, &snarl_finder);
        SnarlDistanceIndexClusterer clusterer(distance_index, &graph);
        
        //graph.to_dot(cerr);

        SECTION( "Seeds on chain nodes" ) {
 
            vector<pos_t> positions;
            positions.emplace_back(1, false, 0);
            positions.emplace_back(3, false, 0);
            positions.emplace_back(6, false, 0);
            //all are in the same cluster
            vector<SnarlDistanceIndexClusterer::Seed> seeds;
            for (pos_t pos : positions) {
                ZipCode zipcode;
                zipcode.fill_in_zipcode(distance_index, pos);
                seeds.push_back({ pos, 0, zipcode});
            }

            ZipCodeTree zip_tree(seeds, distance_index);
            zip_tree.print_self();

            //The tree should be:
            // [pos1 3 pos3 6 pos6]
            //or backwards
            REQUIRE(zip_tree.get_tree_size() == 7);

            //Chain start
            REQUIRE(zip_tree.get_item_at_index(0).type == ZipCodeTree::CHAIN_START);

            //first seed 
            REQUIRE(zip_tree.get_item_at_index(1).type == ZipCodeTree::SEED);

            //distance between them
            REQUIRE(zip_tree.get_item_at_index(2).type == ZipCodeTree::EDGE);
            REQUIRE((zip_tree.get_item_at_index(2).value == 3 ||
                    zip_tree.get_item_at_index(2).value == 6));

            //the next seed
            REQUIRE(zip_tree.get_item_at_index(3).type == ZipCodeTree::SEED);

            //distance between them
            REQUIRE(zip_tree.get_item_at_index(4).type == ZipCodeTree::EDGE);
            REQUIRE((zip_tree.get_item_at_index(4).value == 3 ||
                    zip_tree.get_item_at_index(4).value == 6));

            //the last seed
            REQUIRE(zip_tree.get_item_at_index(5).type == ZipCodeTree::SEED);

            //Chain end
            REQUIRE(zip_tree.get_item_at_index(6).type == ZipCodeTree::CHAIN_END);
        }
        SECTION( "One seed on snarl" ) {
 
            vector<pos_t> positions;
            positions.emplace_back(1, false, 0);
            positions.emplace_back(2, false, 1);
            positions.emplace_back(3, false, 0);
            positions.emplace_back(6, false, 0);
            //all are in the same cluster
            vector<SnarlDistanceIndexClusterer::Seed> seeds;
            for (pos_t pos : positions) {
                ZipCode zipcode;
                zipcode.fill_in_zipcode(distance_index, pos);
                seeds.push_back({ pos, 0, zipcode});
            }

            ZipCodeTree zip_tree(seeds, distance_index);
            zip_tree.print_self();

            //The tree should be:
            // [pos1 0 ( 0 [ 2 pos2 6 ] 0 0 1 ) 0  pos3 6 pos6]
            //or backwards
            REQUIRE(zip_tree.get_tree_size() == 19);
        }
        SECTION( "Three seeds on snarl" ) {
 
            vector<pos_t> positions;
            positions.emplace_back(1, false, 0);
            positions.emplace_back(2, false, 1);
            positions.emplace_back(2, false, 2);
            positions.emplace_back(2, false, 4);
            positions.emplace_back(3, false, 0);
            positions.emplace_back(6, false, 0);
            //all are in the same cluster
            vector<SnarlDistanceIndexClusterer::Seed> seeds;
            for (pos_t pos : positions) {
                ZipCode zipcode;
                zipcode.fill_in_zipcode(distance_index, pos);
                seeds.push_back({ pos, 0, zipcode});
            }

            ZipCodeTree zip_tree(seeds, distance_index);
            zip_tree.print_self();

            //The tree should be:
            // [pos1 0 ( 0 [ 2 pos2 x pos2 x pos2 6 ] 0 0 1 ) 0  pos3 6 pos6]
            //or backwards
            REQUIRE(zip_tree.get_tree_size() == 23);
        }
        SECTION( "Two children of a snarl" ) {
 
            vector<pos_t> positions;
            positions.emplace_back(1, false, 0);
            positions.emplace_back(3, false, 0);
            positions.emplace_back(4, false, 0);
            positions.emplace_back(5, false, 0);
            positions.emplace_back(5, false, 1);
            positions.emplace_back(6, false, 0);
            //all are in the same cluster
            vector<SnarlDistanceIndexClusterer::Seed> seeds;
            for (pos_t pos : positions) {
                ZipCode zipcode;
                zipcode.fill_in_zipcode(distance_index, pos);
                seeds.push_back({ pos, 0, zipcode});
            }

            ZipCodeTree zip_tree(seeds, distance_index);
            zip_tree.print_self();

            //The tree should be:
            // [pos1 0  pos3 0 ( 0 [ 0 pos4 3 ] inf 0 [ 0 pos5 1 pos5 2 ] 3 0 0 2) 0 pos6]
            //or backwards
            REQUIRE(zip_tree.get_tree_size() == 29);
        }
        SECTION( "Only snarls in a snarl" ) {
 
            vector<pos_t> positions;
            positions.emplace_back(2, false, 0);
            positions.emplace_back(4, false, 0);
            positions.emplace_back(5, false, 0);
            positions.emplace_back(5, false, 1);

            //all are in the same cluster
            vector<SnarlDistanceIndexClusterer::Seed> seeds;
            for (pos_t pos : positions) {
                ZipCode zipcode;
                zipcode.fill_in_zipcode(distance_index, pos);
                seeds.push_back({ pos, 0, zipcode});
            }

            ZipCodeTree zip_tree(seeds, distance_index);
            zip_tree.print_self();

            //The tree should be:
            // [( 0 [ 0 pos2 7 ] 0 0 1) 3 ( 0 [0 pos4 3] inf 0 [0 pos5 1 pos5 2 ] 3 0 0 2 )]
            //or backwards
            REQUIRE(zip_tree.get_tree_size() == 35);
        }
    }
    TEST_CASE( "zip tree non-simple DAG", "[zip_tree]" ) {
        VG graph;

        Node* n1 = graph.create_node("GCA");
        Node* n2 = graph.create_node("GCAA");
        Node* n3 = graph.create_node("GCAGGT");
        Node* n4 = graph.create_node("GC");
        Node* n5 = graph.create_node("GC");
        Node* n6 = graph.create_node("GCA");
        Node* n7 = graph.create_node("GCA");
        Node* n8 = graph.create_node("GCAGGGGGGGGGGGGAA");

        Edge* e1 = graph.create_edge(n1, n2);
        Edge* e2 = graph.create_edge(n1, n3);
        Edge* e3 = graph.create_edge(n2, n3);
        Edge* e4 = graph.create_edge(n3, n4);
        Edge* e5 = graph.create_edge(n3, n7);
        Edge* e6 = graph.create_edge(n3, n8);
        Edge* e7 = graph.create_edge(n4, n5);
        Edge* e8 = graph.create_edge(n4, n6);
        Edge* e9 = graph.create_edge(n5, n6);
        Edge* e10 = graph.create_edge(n6, n7);
        Edge* e11 = graph.create_edge(n7, n8);


        IntegratedSnarlFinder snarl_finder(graph);
        SnarlDistanceIndex distance_index;
        fill_in_distance_index(&distance_index, &graph, &snarl_finder);
        SnarlDistanceIndexClusterer clusterer(distance_index, &graph);

        ofstream out ("testGraph.hg");
        graph.serialize(out);

        
        //graph.to_dot(cerr);

        SECTION( "Seeds on chain nodes" ) {
 
            vector<pos_t> positions;
            positions.emplace_back(1, false, 0);
            positions.emplace_back(2, false, 0);
            positions.emplace_back(3, false, 0);
            positions.emplace_back(3, false, 1);
            positions.emplace_back(4, false, 0);
            positions.emplace_back(5, false, 0);
            positions.emplace_back(6, false, 0);
            positions.emplace_back(7, false, 1);
            positions.emplace_back(8, false, 0);
            positions.emplace_back(8, false, 2);
            //all are in the same cluster
            vector<SnarlDistanceIndexClusterer::Seed> seeds;
            for (pos_t pos : positions) {
                ZipCode zipcode;
                zipcode.fill_in_zipcode(distance_index, pos);
                seeds.push_back({ pos, 0, zipcode});
            }

            ZipCodeTree zip_tree(seeds, distance_index);
            zip_tree.print_self();
        }
    }
}
}