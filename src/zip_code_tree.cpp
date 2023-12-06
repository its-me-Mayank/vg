//#define DEBUG_ZIP_CODE_TREE
//#define PRINT_NON_DAG_SNARLS
//#define DEBUG_ZIP_CODE_SORTING

#include "zip_code_tree.hpp"
#include <structures/union_find.hpp>

#include "crash.hpp"

//#define debug_parse

using namespace std;
namespace vg {


void ZipCodeForest::open_chain(forest_growing_state_t& forest_state, 
                               const size_t& depth, size_t seed_index, bool chain_is_reversed) {
    //If this is the start of a new chain
#ifdef DEBUG_ZIP_CODE_TREE
    cerr << "\t\tOpen new chain at depth " << depth << endl;
#endif
    const Seed& current_seed = forest_state.seeds->at(seed_index);

    size_t current_max_depth = current_seed.zipcode_decoder->max_depth();

    if (depth == 0) {
        //If this is the start of a new top-level chain, make a new tree, which will be the new active tree
#ifdef DEBUG_ZIP_CODE_TREE
        cerr << "Add a new tree" << endl;
#endif
        if (forest_state.active_zip_tree_i == std::numeric_limits<size_t>::max() 
            || trees[forest_state.active_zip_tree_i].zip_code_tree.size() != 0) {
            //Don't add a new tree if the current one is empty
            trees.emplace_back();
            forest_state.active_zip_tree_i = trees.size()-1;
        }
    } else  {
        //If this is the start of a non-root chain, then it is the child of a snarl and 
        //we need to find the distances to the previous things in the snarl
        //The distances will be filled in when the chain is closed, since parts of the
        //chain may be removed, and the distance to the start of the chain may change
        for (size_t i = 0 ; i < forest_state.sibling_indices_at_depth[depth-1].size() ; i++) {
            trees[forest_state.active_zip_tree_i].zip_code_tree.emplace_back(ZipCodeTree::EDGE, 
                  std::numeric_limits<size_t>::max(),
                  false);
        }

    }

    //Now record the start of this chain
    trees[forest_state.active_zip_tree_i].zip_code_tree.emplace_back(ZipCodeTree::CHAIN_START, 
                                                                     std::numeric_limits<size_t>::max(), false);

    //Remember the start of the chain and its prefix sum value as a child of the chain
    forest_state.sibling_indices_at_depth[depth].push_back({ZipCodeTree::CHAIN_START, 0});

    //And, if it is the child of a snarl, then remember the chain as a child of the snarl
    if (depth != 0) {
        forest_state.sibling_indices_at_depth[depth-1].push_back({ZipCodeTree::CHAIN_START,
                                                     trees[forest_state.active_zip_tree_i].zip_code_tree.size()-1});

        //The distances in the snarl include the distances from the first/last children in the
        //chain to the ends of the chains
        //
        //Remember the distance to the start of this child in the chain
        forest_state.sibling_indices_at_depth[depth-1].back().distances.first 
                = forest_state.sort_values_by_seed[seed_index].get_distance_value(); 

        //Remember the opening of this chain, and if its first child was far enough from the start to 
        //start a new subtree
        forest_state.open_chains.emplace_back(trees[forest_state.active_zip_tree_i].zip_code_tree.size()-1, 
                 forest_state.sibling_indices_at_depth[depth-1].back().distances.first > forest_state.distance_limit);
    }
}

void ZipCodeForest::close_chain(forest_growing_state_t& forest_state, 
                                const size_t& depth, const Seed& last_seed, bool chain_is_reversed) {

#ifdef DEBUG_ZIP_CODE_TREE
    cerr << "\t\tclose a chain at depth " << depth << endl;
#endif
    if (trees[forest_state.active_zip_tree_i].zip_code_tree.back().get_type() == ZipCodeTree::CHAIN_START) {
        //If the chain was empty.
        //This could happen if there was only a snarl in it and it got removed

        //Take out the CHAIN_START
        trees[forest_state.active_zip_tree_i].zip_code_tree.pop_back();

        //Forget about this chain in its parent snarl
        if (trees[forest_state.active_zip_tree_i].zip_code_tree.back().get_type() == ZipCodeTree::EDGE) {
            forest_state.sibling_indices_at_depth[depth-1].pop_back();
        }

        //If the chain was part of a snarl, then take out the edges
        while (trees[forest_state.active_zip_tree_i].zip_code_tree.size() > 0 &&
               trees[forest_state.active_zip_tree_i].zip_code_tree.back().get_type() == ZipCodeTree::EDGE) {
            trees[forest_state.active_zip_tree_i].zip_code_tree.pop_back();
        }

        //Forget about the chain
        if (depth != 0) {
            forest_state.open_chains.pop_back();
        }

    } else {
        //Add the end of the chain to the zip code tree
        trees[forest_state.active_zip_tree_i].zip_code_tree.emplace_back(ZipCodeTree::CHAIN_END, 
                                                                         std::numeric_limits<size_t>::max(), 
                                                                         false);

        if (depth == 0) {
            return;
        }


        // For chains in snarls, we want to know the distance from the last thing
        // in the chain to the end of the chain
        // If the distance is greater than the distance limit, we may make a new tree
        // for a slice of the chain.
        // If the chain remains in the snarl, we need to remember the distance to the end
        // of the chain to add to the relevant distances in the parent snarl.
        // These distances will be stored in forest_state.sibling_indices_at_depth

#ifdef DEBUG_ZIP_CODE_TREE
        assert(forest_state.sibling_indices_at_depth[depth-1].size() > 0);
        assert(forest_state.sibling_indices_at_depth[depth-1].back().type == ZipCodeTree::CHAIN_START);
#endif
        //Only add the distance for a non-root chain

        //If this is reversed, then the distance should be the distance to the start of 
        //the chain. Otherwise, the distance to the end
        //The value that got stored in forest_state.sibling_indices_at_depth was the prefix sum
        //traversing the chain according to its orientation in the tree, so either way
        //the distance is the length of the chain - the prefix sum
        size_t distance_to_chain_end = SnarlDistanceIndex::minus(last_seed.zipcode_decoder->get_length(depth),
                                      forest_state.sibling_indices_at_depth[depth].back().value);
        bool add_distances = true;
        if (distance_to_chain_end > forest_state.distance_limit && forest_state.open_chains.back().second) {
            //If the distance to the end is greater than the distance limit, and there was something
            // in the chain with a large distance to the thing before it, then splice out a chain slice

            //Add a new tree
            trees.emplace_back();

            if (trees[forest_state.active_zip_tree_i].zip_code_tree.at(forest_state.open_chains.back().first).get_type() 
                    == ZipCodeTree::CHAIN_START) {
                //If we're copying the entire chain child of a snarl
#ifdef DEBUG_ZIP_CODE_TREE
                cerr << "Copy the entire chain to a new subtree" << endl;
#endif
                if (forest_state.open_chains.back().first != 0) {

                    //Copy everything in the child chain into the new tree
                    trees.back().zip_code_tree.insert(trees.back().zip_code_tree.end(),
                        std::make_move_iterator(trees[forest_state.active_zip_tree_i].zip_code_tree.begin() 
                            + forest_state.open_chains.back().first),
                        std::make_move_iterator(trees[forest_state.active_zip_tree_i].zip_code_tree.end()));

                    //Remove the child chain from the active tree
                    trees[forest_state.active_zip_tree_i].zip_code_tree.erase(
                           trees[forest_state.active_zip_tree_i].zip_code_tree.begin() 
                                    + forest_state.open_chains.back().first,
                           trees[forest_state.active_zip_tree_i].zip_code_tree.end());

                    //The chain no longer exists in the snarl, so forget that it exists
                    forest_state.sibling_indices_at_depth[depth-1].pop_back();

                    //And remove all the edges
                    while (trees[forest_state.active_zip_tree_i].zip_code_tree.size() > 0
                            && trees[forest_state.active_zip_tree_i].zip_code_tree.back().get_type() == ZipCodeTree::EDGE) {
                        trees[forest_state.active_zip_tree_i].zip_code_tree.pop_back();
                    }
                }
#ifdef DEBUG_ZIP_CODE_TREE
                 assert((trees[forest_state.active_zip_tree_i].zip_code_tree.back().get_type() == ZipCodeTree::CHAIN_END ||
                         trees[forest_state.active_zip_tree_i].zip_code_tree.back().get_type() == ZipCodeTree::SNARL_START));
#endif
                 // Since we took out the whole chain, we shouldn't add the distances later
                 add_distances = false;
             } else {
#ifdef DEBUG_ZIP_CODE_TREE
                cerr << "Copy a slice from the middle of the chain to the end" << endl;
                assert((trees[forest_state.active_zip_tree_i].zip_code_tree.at(forest_state.open_chains.back().first).get_type() 
                                    == ZipCodeTree::SEED || 
                        trees[forest_state.active_zip_tree_i].zip_code_tree.at(forest_state.open_chains.back().first).get_type() 
                                    == ZipCodeTree::SNARL_START));
#endif
                //We're copying a slice of the chain from the middle to the end
                //Start a new chain in the new subtree
                trees.back().zip_code_tree.emplace_back(ZipCodeTree::CHAIN_START, 
                                                      std::numeric_limits<size_t>::max(), false);

                //Copy everything in the slice into the new tree
                trees.back().zip_code_tree.insert(trees.back().zip_code_tree.end(),
                    std::make_move_iterator(trees[forest_state.active_zip_tree_i].zip_code_tree.begin() 
                                            + forest_state.open_chains.back().first),
                    std::make_move_iterator(trees[forest_state.active_zip_tree_i].zip_code_tree.end()));
                //Erase the slice
                trees[forest_state.active_zip_tree_i].zip_code_tree.erase(
                        trees[forest_state.active_zip_tree_i].zip_code_tree.begin() 
                                + forest_state.open_chains.back().first,
                        trees[forest_state.active_zip_tree_i].zip_code_tree.end());


                //Take out the last edge
                size_t last_edge = trees[forest_state.active_zip_tree_i].zip_code_tree.back().get_value();
                trees[forest_state.active_zip_tree_i].zip_code_tree.pop_back();

                //Close the chain in the original active tree
                trees[forest_state.active_zip_tree_i].zip_code_tree.emplace_back(ZipCodeTree::CHAIN_END, 
                                            std::numeric_limits<size_t>::max(), false);

                //Update the distance to the end of the chain to be the distance from the previous child 
                size_t last_length = depth == last_seed.zipcode_decoder->max_depth() 
                                   ? 0 
                                   : last_seed.zipcode_decoder->get_length(depth+1);

                distance_to_chain_end = SnarlDistanceIndex::sum(distance_to_chain_end, 
                                        SnarlDistanceIndex::sum(last_edge,
                                                                last_length));
            }
        }
        if (add_distances) {
            // If this chain (or chain slice) remains in the snarl, then add the distances
            // in the snarl

            //remember the distance to the end to be used in snarl distances
            forest_state.sibling_indices_at_depth[depth-1].back().distances.second = distance_to_chain_end;

            bool snarl_is_reversed = forest_state.open_intervals[forest_state.open_intervals.size()-2].is_reversed;
            bool is_cyclic_snarl =  forest_state.open_intervals[forest_state.open_intervals.size()-2].code_type 
                                        == ZipCode::CYCLIC_SNARL;

            add_snarl_distances(forest_state, depth-1, last_seed, chain_is_reversed, snarl_is_reversed, 
                                false, is_cyclic_snarl);
        }
        //We've closed a chain, so take out the latest open chain
        forest_state.open_chains.pop_back();
    }
}

void ZipCodeForest::add_child_to_chain(forest_growing_state_t& forest_state, 
                       const size_t& depth, const size_t& seed_index, bool child_is_reversed,
                       bool chain_is_reversed, bool is_cyclic_snarl) {
    const Seed& current_seed = forest_state.seeds->at(seed_index);
    //For these things, we need to remember the offset in the node/chain

    ZipCode::code_type_t current_type = current_seed.zipcode_decoder->get_code_type(depth);

    //Is this chain actually a node pretending to be a chain
    bool is_trivial_chain = current_type == ZipCode::CHAIN && depth == current_seed.zipcode_decoder->max_depth();

    //For a root node or trivial chain, the "chain" is actually just the node, so the depth 
    // of the chain we're working on is the same depth. Otherwise, the depth is depth-1
    size_t chain_depth = is_trivial_chain || current_type == ZipCode::ROOT_NODE ? depth : depth-1;

    ///////////////// Get the offset in the parent chain (or node)
    size_t current_offset;


    //First, get the prefix sum in the chain + offset in the node
    if (current_type == ZipCode::ROOT_NODE || current_type == ZipCode::NODE || is_trivial_chain) {
        //For a node, this is still the distance used to sort on
        current_offset = forest_state.sort_values_by_seed[seed_index].get_distance_value();
    } else {
        //Otherwise, get the distance to the start or end of the chain

        current_offset = chain_is_reversed 
                ? SnarlDistanceIndex::minus(current_seed.zipcode_decoder->get_length(chain_depth) ,
                                            SnarlDistanceIndex::sum(
                                                current_seed.zipcode_decoder->get_offset_in_chain(depth),
                                                current_seed.zipcode_decoder->get_length(depth))) 
                : current_seed.zipcode_decoder->get_offset_in_chain(depth);
    }


    /////////////////////// Get the offset of the previous thing in the parent chain/node
    size_t previous_offset = forest_state.sibling_indices_at_depth[chain_depth][0].value;


#ifdef DEBUG_ZIP_CODE_TREE
    assert(forest_state.sibling_indices_at_depth[chain_depth].size() == 1);
#endif

    ///////////////////// Record the distance from the previous thing in the chain/node
    //       Or add a new tree if the distance is too far
    if (chain_depth > 0 && forest_state.sibling_indices_at_depth[chain_depth][0].type == ZipCodeTree::CHAIN_START){
        //If this is the first thing in a non-root chain or node, remember the distance to the 
        //start of the chain/node.
        //This distance will be added to distances in the parent snarl
        forest_state.sibling_indices_at_depth[chain_depth-1][0].distances.first = current_offset;

        //Also update the last chain opened
        forest_state.open_chains.back().second = current_offset > forest_state.distance_limit;


    } else if (forest_state.sibling_indices_at_depth[chain_depth][0].type != ZipCodeTree::CHAIN_START) {
        //for everything except the first thing in a node/chain, we need to add the edge

        size_t distance_between;
        if (previous_offset > current_offset) {
            //If the parent is a multicomponent chain, then they might be in different components
            //TODO: This won't catch all cases of different components in the chain
            distance_between = std::numeric_limits<size_t>::max();
        } else {
            distance_between = current_offset - previous_offset;
        }

        if (chain_depth == 0 && distance_between > forest_state.distance_limit) {
            //The next thing in the zip tree will be the first seed (or snarl) in a top-level chain, 
            // so start a new tree
#ifdef DEBUG_ZIP_CODE_TREE
            cerr << "Start a new tree in the forest" << endl;
#endif
            //Close the previous chain
            trees[forest_state.active_zip_tree_i].zip_code_tree.emplace_back(ZipCodeTree::CHAIN_END, 
                                                                         std::numeric_limits<size_t>::max(), 
                                                                         false);

            if (forest_state.active_zip_tree_i == std::numeric_limits<size_t>::max() 
                || trees[forest_state.active_zip_tree_i].zip_code_tree.size() != 0) {
                //Add a new tree and make sure it is the new active tree
                trees.emplace_back();
                forest_state.active_zip_tree_i = trees.size()-1;
            }

            //Add the start of the new chain
            trees[forest_state.active_zip_tree_i].zip_code_tree.emplace_back(ZipCodeTree::CHAIN_START, 
                                                                         std::numeric_limits<size_t>::max(), 
                                                                         false);

            //The first sibling in the chain is now the chain start, not the previous seed, so replace it
            forest_state.sibling_indices_at_depth[chain_depth].pop_back();
            forest_state.sibling_indices_at_depth[chain_depth].push_back({ZipCodeTree::CHAIN_START, 0}); 

        } else if (distance_between > forest_state.distance_limit) { 
            //If this is too far from the previous thing, but inside a snarl

            if (forest_state.open_chains.back().second) {
#ifdef DEBUG_ZIP_CODE_TREE
                cerr << "\tMake a new slice of the chain at depth " << depth << endl;
#endif
                //If the current chain slice was also too far away from the thing before it
                // then copy the slice
                if (trees[forest_state.active_zip_tree_i].zip_code_tree.at(forest_state.open_chains.back().first).get_type() 
                        == ZipCodeTree::CHAIN_START) {
                    //If the slice starts at the start of the chain and ends at the previous seed

                    //Copy everything in the slice to the end of a new tree
                    trees.emplace_back();
                    trees.back().zip_code_tree.insert(trees.back().zip_code_tree.end(),
                        std::make_move_iterator(trees[forest_state.active_zip_tree_i].zip_code_tree.begin() 
                                                + forest_state.open_chains.back().first),
                        std::make_move_iterator(trees[forest_state.active_zip_tree_i].zip_code_tree.end()));

                    //Erase the slice from the active tree
                    trees[forest_state.active_zip_tree_i].zip_code_tree.erase(
                        trees[forest_state.active_zip_tree_i].zip_code_tree.begin() 
                            + forest_state.open_chains.back().first,
                        trees[forest_state.active_zip_tree_i].zip_code_tree.end());

                    //Add the end of the chain to the new slice
                    trees.back().zip_code_tree.emplace_back(ZipCodeTree::CHAIN_END, 
                                                          std::numeric_limits<size_t>::max(), 
                                                          false);

                    //Add back the start of the chain
                    trees[forest_state.active_zip_tree_i].zip_code_tree.emplace_back(ZipCodeTree::CHAIN_START, 
                                                                                 std::numeric_limits<size_t>::max(), 
                                                                                 false);

                    //Update the chain as a child of the snarl
#ifdef DEBUG_ZIP_CODE_TREE
                    assert(forest_state.sibling_indices_at_depth[chain_depth-1].back().type == ZipCodeTree::CHAIN_START);
                    //The value should be the index of the last seed, which is the first seed in the new tree
                    assert(forest_state.sibling_indices_at_depth[chain_depth-1].back().value 
                                == trees[forest_state.active_zip_tree_i].zip_code_tree.size()-1);
                    assert(forest_state.open_chains.back().second);

#endif
                    forest_state.sibling_indices_at_depth[chain_depth-1].back().distances.first = current_offset;

                    //Don't need to update open_chains, since the next slice will also start at the chain start and be able to make 
                    //a new thing

                } else {
#ifdef DEBUG_ZIP_CODE_TREE
                    assert((trees[forest_state.active_zip_tree_i].zip_code_tree.at(forest_state.open_chains.back().first).get_type() 
                               == ZipCodeTree::SEED ||
                           trees[forest_state.active_zip_tree_i].zip_code_tree.at(forest_state.open_chains.back().first).get_type() 
                                == ZipCodeTree::SNARL_START));
#endif
                    //If the slice starts and ends in the middle of the chain

                    //Copy everything in the slice to a new chain in a new tree
                    trees.emplace_back();
                    trees.back().zip_code_tree.emplace_back(ZipCodeTree::CHAIN_START, 
                                                          std::numeric_limits<size_t>::max(), 
                                                          false);
                    trees.back().zip_code_tree.insert(trees.back().zip_code_tree.end(),
                        std::make_move_iterator(trees[forest_state.active_zip_tree_i].zip_code_tree.begin() 
                                                 + forest_state.open_chains.back().first),
                        std::make_move_iterator(trees[forest_state.active_zip_tree_i].zip_code_tree.end()));

                    //Erase the slice from the active tree
                    trees[forest_state.active_zip_tree_i].zip_code_tree.erase(
                        trees[forest_state.active_zip_tree_i].zip_code_tree.begin() + forest_state.open_chains.back().first,
                        trees[forest_state.active_zip_tree_i].zip_code_tree.end());
                    //Add the end of the chain to the new slice
                    trees.back().zip_code_tree.emplace_back(ZipCodeTree::CHAIN_END, 
                                                          std::numeric_limits<size_t>::max(), 
                                                          false);
                    //The original tree gets an edge with infinite length, since it will be bigger than the distance limit anyway
#ifdef DEBUG_ZIP_CODE_TREE
                    assert(trees[forest_state.active_zip_tree_i].zip_code_tree.back().get_type() == ZipCodeTree::EDGE);
#endif
                    trees[forest_state.active_zip_tree_i].zip_code_tree.pop_back();
                    trees[forest_state.active_zip_tree_i].zip_code_tree.emplace_back(ZipCodeTree::EDGE, 
                                                                                 std::numeric_limits<size_t>::max(),
                                                                                 false);

                    //Remember the next seed or snarl that gets added as the start of a new chain slice
                    forest_state.open_chains.pop_back();
                    forest_state.open_chains.emplace_back(trees[forest_state.active_zip_tree_i].zip_code_tree.size(), true);
                }
            } else {
#ifdef DEBUG_ZIP_CODE_TREE
                cerr << "The slice didn't get copied but maybe start a new slice here" << endl;
#endif
                //If the slice doesn't get copied because it is still connected at the front,
                //add the edge to the chain and remember that it could start a new slice

                trees[forest_state.active_zip_tree_i].zip_code_tree.emplace_back(ZipCodeTree::EDGE, distance_between, false);

                //Remember the next seed or snarl that gets added as the start of a new chain slice
                forest_state.open_chains.pop_back();
                forest_state.open_chains.emplace_back(trees[forest_state.active_zip_tree_i].zip_code_tree.size(), true);
            }

        } else {
            //If we didn't start a new tree, then add the edge
            trees[forest_state.active_zip_tree_i].zip_code_tree.emplace_back(ZipCodeTree::EDGE, distance_between, false);
        }
    }

    /////////////////////////////Record this thing in the chain
    if (current_type == ZipCode::NODE || current_type == ZipCode::ROOT_NODE || is_trivial_chain) {
#ifdef DEBUG_ZIP_CODE_TREE
        cerr << "\t\tContinue node/chain with seed " << current_seed.pos << " at depth " << depth << endl;
#endif
        //If this was a node, just remember the seed
        trees[forest_state.active_zip_tree_i].zip_code_tree.emplace_back(ZipCodeTree::SEED, 
                                                                         seed_index, 
                                                                         child_is_reversed != is_rev(current_seed.pos));
    } else {

        open_snarl(forest_state, depth, is_cyclic_snarl); 

        //For finding the distance to the next thing in the chain, the offset
        //stored should be the offset of the end bound of the snarl, so add the 
        //length of the snarl
        current_offset = SnarlDistanceIndex::sum(current_offset,
            current_seed.zipcode_decoder->get_length(depth));

    }

    //Remember this thing for the next sibling in the chain
    forest_state.sibling_indices_at_depth[chain_depth].pop_back();
    forest_state.sibling_indices_at_depth[chain_depth].push_back({(
         current_type == ZipCode::NODE || current_type == ZipCode::ROOT_NODE) ? ZipCodeTree::SEED 
                                                                              : ZipCodeTree::SNARL_START,
         current_offset}); 
#ifdef DEBUG_ZIP_CODE_TREE
    cerr << "Add sibling with type " << current_type << endl;
#endif

}

void ZipCodeForest::open_snarl(forest_growing_state_t& forest_state, const size_t& depth, bool is_cyclic_snarl) {
#ifdef DEBUG_ZIP_CODE_TREE
        cerr << "\t\tOpen new snarl at depth " << depth << endl;
#endif
    //If this was a snarl, record the start of the snarl
    trees[forest_state.active_zip_tree_i].zip_code_tree.emplace_back(ZipCodeTree::SNARL_START, 
                                                                     std::numeric_limits<size_t>::max(), false);
    
    if (depth != 0 && !is_cyclic_snarl) {
        //Remember the start of the snarl to find distances later
        //Don't do this for a root snarl because technically there is no start node so there are no distances to it
        forest_state.sibling_indices_at_depth[depth].push_back({ZipCodeTree::SNARL_START, 
                                                                std::numeric_limits<size_t>::max()});
    }
}

void ZipCodeForest::close_snarl(forest_growing_state_t& forest_state, 
                                const size_t& depth, const Seed& last_seed, bool last_is_reversed, bool is_cyclic_snarl) {
#ifdef DEBUG_ZIP_CODE_TREE
    cerr << "\t\tclose a snarl at depth " << depth << endl;
#endif

    if (depth == 0) {
        //If this is a root snarl, then we don't need distances so just close it 
        trees[forest_state.active_zip_tree_i].zip_code_tree.emplace_back(ZipCodeTree::SNARL_END, 
                                                                         std::numeric_limits<size_t>::max(), 
                                                                         false);

    } else if (forest_state.sibling_indices_at_depth[depth].size() == 1) {
        //Since some of the children of the snarl may have been removed to separate subtrees,
        //the snarl may actually be empty now
        //If there is only one "child" (the snarl start), then the snarl is actually empty, so delete it

#ifdef DEBUG_ZIP_CODE_TREE
        cerr << "\t\t\tThe snarl is actually empty so remove it" << endl;
#endif
        //Take out the edges
        while (trees[forest_state.active_zip_tree_i].zip_code_tree.size() > 0
                && trees[forest_state.active_zip_tree_i].zip_code_tree.back().get_type() == ZipCodeTree::EDGE) {
            trees[forest_state.active_zip_tree_i].zip_code_tree.pop_back();
        }
#ifdef DEBUG_ZIP_CODE_TREE
        assert(trees[forest_state.active_zip_tree_i].zip_code_tree.back().get_type() == ZipCodeTree::SNARL_START);
#endif        
        //Pop the snarl start out
        trees[forest_state.active_zip_tree_i].zip_code_tree.pop_back();

        //If this was the first thing in the chain, then we're done. Otherwise, there was an edge to remove
        if (trees[forest_state.active_zip_tree_i].zip_code_tree.back().get_type() == ZipCodeTree::EDGE) {
            //If the snarl was in the middle of a chain, then we need to take out the edge and update
            //the previous thing in the chain with its prefix sum

            //This was the distance from the last thing to the start of this snarl
            size_t previous_edge = trees[forest_state.active_zip_tree_i].zip_code_tree.back().get_value();
            trees[forest_state.active_zip_tree_i].zip_code_tree.pop_back();

            //This is the distance from the start of the chain to the end of the snarl
            size_t snarl_prefix_sum = forest_state.sibling_indices_at_depth[depth-1].back().value;
            forest_state.sibling_indices_at_depth[depth-1].pop_back();

            //Snarl prefix sum is now the distance from the start of the chain to the start of the snarl
            snarl_prefix_sum = SnarlDistanceIndex::minus(snarl_prefix_sum, last_seed.zipcode_decoder->get_length(depth));

            //Now update forest_state.sibling_indices_at_depth to be the previous thing in the chain
            forest_state.sibling_indices_at_depth[depth-1].push_back({
                trees[forest_state.active_zip_tree_i].zip_code_tree.back().get_type() == ZipCodeTree::SEED 
                    ? ZipCodeTree::SEED 
                    : ZipCodeTree::SNARL_START,
                SnarlDistanceIndex::minus(snarl_prefix_sum, previous_edge)});

            if (depth > 0 && forest_state.open_chains.size() > 0 
                && forest_state.open_chains.back().first >= trees[forest_state.active_zip_tree_i].zip_code_tree.size()) {
                //If there was a chain slice that could have started at this snarl
#ifdef DEBUG_ZIP_CODE_TREE
                assert(forest_state.open_chains.back().second);
#endif
                //Find the start of the previous child
                size_t previous_index = trees[forest_state.active_zip_tree_i].zip_code_tree.size() - 1;
                bool found_sibling = false;
                bool opened_snarl = false;
                while (!found_sibling) {
                    if (!opened_snarl && 
                        trees[forest_state.active_zip_tree_i].zip_code_tree.at(previous_index).get_type() 
                                 == ZipCodeTree::SEED) {
                        found_sibling = true;
                    } else if (trees[forest_state.active_zip_tree_i].zip_code_tree.at(previous_index).get_type() 
                                    == ZipCodeTree::SNARL_END) {
                        opened_snarl = true;
                        previous_index--;
                    } else if ((trees[forest_state.active_zip_tree_i].zip_code_tree.at(previous_index).get_type()
                                     == ZipCodeTree::SNARL_START)) {
                        found_sibling = true;
                    } else {
                        previous_index--;
                    }
                }
                if (trees[forest_state.active_zip_tree_i].zip_code_tree.at(previous_index-1).get_type() 
                            == ZipCodeTree::CHAIN_START) {
                    previous_index--;
                }
#ifdef DEBUG_ZIP_CODE_TREE
                assert(( trees[forest_state.active_zip_tree_i].zip_code_tree.at(previous_index).get_type()
                                == ZipCodeTree::SEED 
                         ||
                         trees[forest_state.active_zip_tree_i].zip_code_tree.at(previous_index).get_type() 
                                == ZipCodeTree::SNARL_START 
                         ||
                         trees[forest_state.active_zip_tree_i].zip_code_tree.at(previous_index).get_type() 
                                == ZipCodeTree::CHAIN_START));
                cerr << "New start of previous open chain: " << previous_index << endl;;
#endif
                forest_state.open_chains.back().first = previous_index;
                
            }
#ifdef DEBUG_ZIP_CODE_TREE
            assert(forest_state.sibling_indices_at_depth[depth-1].back().value >= 0);
#endif
        } else {
            //If this was the first thing in the chain, update the previous sibling in the chain to be the start of the chain
#ifdef DEBUG_ZIP_CODE_TREE
            assert(trees[forest_state.active_zip_tree_i].zip_code_tree.back().get_type() == ZipCodeTree::CHAIN_START);
#endif
            forest_state.sibling_indices_at_depth[depth-1].pop_back();
            forest_state.sibling_indices_at_depth[depth-1].push_back({ ZipCodeTree::CHAIN_START, 0});
        }
    } else {

        //If this is the end of the snarl that still has children, then we need to save the distances to 
        //all previous children of the snarl
        trees[forest_state.active_zip_tree_i].zip_code_tree.resize(trees[forest_state.active_zip_tree_i].zip_code_tree.size() 
                                                                    + forest_state.sibling_indices_at_depth[depth].size());
       
        add_snarl_distances(forest_state, depth, last_seed, last_is_reversed, last_is_reversed, true,
                            is_cyclic_snarl);

        //Note the count of children and the end of the snarl
        trees[forest_state.active_zip_tree_i].zip_code_tree.emplace_back(ZipCodeTree::NODE_COUNT, 
                                                                     forest_state.sibling_indices_at_depth[depth].size()-1, 
                                                                     false);
        trees[forest_state.active_zip_tree_i].zip_code_tree.emplace_back(ZipCodeTree::SNARL_END, 
                                                                     std::numeric_limits<size_t>::max(), 
                                                                     false);
     }
}

void ZipCodeForest::add_snarl_distances(forest_growing_state_t& forest_state, const size_t& depth, 
                                const Seed& seed, bool child_is_reversed, bool snarl_is_reversed, 
                                bool to_snarl_end, bool is_cyclic_snarl) {

    // This adds distances from everything in the snarl to the last thing in the snarl, which is either the snarl end
    // or a chain child of the snarl


    //Distances from this child to add 
    size_t distance_to_chain_end = to_snarl_end ? 0 : forest_state.sibling_indices_at_depth[depth].back().distances.second;
    size_t distance_to_chain_start = to_snarl_end ? 0 : forest_state.sibling_indices_at_depth[depth].back().distances.first;
    
    // This is the index of the thing in the snarl right before the distances start. Used to figure out
    // where to put the distances
    size_t last_child_index = to_snarl_end ? trees[forest_state.active_zip_tree_i].zip_code_tree.size()
                                           : forest_state.sibling_indices_at_depth[depth].back().value;
    
    //Now add the distances from the start of the chain to everything before it in the snarl
    
    
    // If this is to the end bound, get the distance to all siblings. If it is to the last child, don't get
    // the distance to itself
    size_t sibling_count = to_snarl_end ? forest_state.sibling_indices_at_depth[depth].size() 
                                        : forest_state.sibling_indices_at_depth[depth].size()-1;
    for ( size_t sibling_i = 0 ; sibling_i < sibling_count ; sibling_i++) {
        const auto& sibling = forest_state.sibling_indices_at_depth[depth][sibling_i];

        if (sibling.type == ZipCodeTree::SNARL_START && !is_cyclic_snarl) {
            //Get the distance to the start (or end if it's reversed) of the snarl
            

            //If we're getting the distance to the end of the snarl, then this is the length of the snarl
            // otherwise, it is the distance from the seed to the start (or end) of the snarl
            size_t snarl_distance = to_snarl_end ? seed.zipcode_decoder->get_length(depth)
                                                 : SnarlDistanceIndex::sum (distance_to_chain_start,
                                                    snarl_is_reversed
                                                        ? seed.zipcode_decoder->get_distance_to_snarl_end(depth+1)
                                                        : seed.zipcode_decoder->get_distance_to_snarl_start(depth+1));

            //Add the edge
            trees[forest_state.active_zip_tree_i].zip_code_tree.at(last_child_index - 1 - sibling_i) = 
                {ZipCodeTree::EDGE, snarl_distance, false};

        } else {
            //Otherwise, the previous thing was another child of the snarl
            //and we need to record the distance between these two
            //TODO: This can be improved for simple snarls
            size_t distance;
            if (seed.zipcode_decoder->get_code_type(depth) == ZipCode::REGULAR_SNARL) {
                //If this is the child of a regular snarl, then the distance between
                //any two chains is inf, and the distance to any bound is 0
                distance = to_snarl_end ? sibling.distances.second : std::numeric_limits<size_t>::max();
            } else {
                size_t seed_i = sibling.value+1;
                while (trees[forest_state.active_zip_tree_i].zip_code_tree[seed_i].get_type() != ZipCodeTree::SEED) {
                    seed_i++;
                }
                auto& sibling_seed = forest_state.seeds->at(trees[forest_state.active_zip_tree_i].zip_code_tree[seed_i].get_value());

                if (to_snarl_end && !is_cyclic_snarl) {

                    distance = SnarlDistanceIndex::sum(sibling.distances.second,
                                          snarl_is_reversed ? sibling_seed.zipcode_decoder->get_distance_to_snarl_start(depth+1)
                                                            : sibling_seed.zipcode_decoder->get_distance_to_snarl_end(depth+1));
                } else {

                    //If to_snarl_end is true, then we want the distance to the end (or start if snarl_is_reversed)
                    // Rank is 0 and the orientation doesn't matter
                    size_t rank2 = to_snarl_end ? (snarl_is_reversed ? 0 : 1) 
                                                : seed.zipcode_decoder->get_rank_in_snarl(depth+1);
                    bool right_side2 = child_is_reversed;

                    //If the sibling is the start, then get the distance to the appropriate bound
                    size_t rank1 = sibling.type == ZipCodeTree::SNARL_START 
                                        ? (snarl_is_reversed ? 1 : 0)
                                        : sibling_seed.zipcode_decoder->get_rank_in_snarl(depth+1);
                    bool right_side1 = !sibling.is_reversed;

                    size_t distance_to_end_of_last_child = sibling.type == ZipCodeTree::SNARL_START ? 0
                                                     : sibling.distances.second;
                    //The bools for this are true if the distance is to/from the right side of the child
                    //We want the right side of 1 (which comes first in the dag ordering) to the left side of 2
                    //relative to the orientation of the snarl
                    net_handle_t snarl_handle = seed.zipcode_decoder->get_net_handle(depth, forest_state.distance_index);
                    distance = SnarlDistanceIndex::sum(SnarlDistanceIndex::sum(
                        forest_state.distance_index->distance_in_snarl(snarl_handle, rank1, right_side1, rank2, right_side2),
                        distance_to_chain_start),
                        distance_to_end_of_last_child);
                }
            }
            trees[forest_state.active_zip_tree_i].zip_code_tree.at(last_child_index - 1 - sibling_i) 
                    = {ZipCodeTree::EDGE, distance, false};
        }
    
    }

    //Remember the orientation of this child for the next time it gets used
    forest_state.sibling_indices_at_depth[depth].back().is_reversed = child_is_reversed;
}

double ZipCodeForest::get_correlation(const vector<pair<size_t, size_t>>& values) {
#ifdef DEBUG_ZIP_CODE_TREE
    cerr << "get correlation from " << values.size() << " values: " << endl;
    for (const auto& x : values) {
        cerr << x.first << "/" << x.second << "\t";
    }
    cerr << endl;
#endif
    if (values.size() == 0) {
        return 0.0;
    }
    
    //This will hold the ranks for each pair in values
    vector<std::pair<size_t, size_t>> ranks (values.size());

    //A vector representing indices into ranks/values
    //This gets sorted first by the first value in the pair and then the second, in order to get the ranks
    //for each value
    vector<size_t> sorted_indices(values.size());
    for(size_t i = 0 ; i < sorted_indices.size() ; i++) {sorted_indices[i] = i;}

    //First, sort by the first value and fill in the ranks
    std::sort(sorted_indices.begin(), sorted_indices.end(), [&] (const size_t& a, const size_t& b) {
        return values[a].first < values[b].first;
    });


    //Sum of all ranks of the first value
    size_t first_rank_sum = 0;

    size_t rank = 0;
    for (size_t i = 0 ; i < sorted_indices.size() ; i++) {
        if (i != 0 && values[sorted_indices[i]].first != values[sorted_indices[i-1]].first) {
            ++rank;
        }
        ranks[sorted_indices[i]].first = rank;
        first_rank_sum += rank;
    }

    //Now do the same thing with the second value - sort and fill in the ranks

    std::sort(sorted_indices.begin(), sorted_indices.end(), [&] (const size_t& a, const size_t& b) {
        return values[a].second < values[b].second;
    });

    size_t second_rank_sum = 0;

    rank = 0;
    for (size_t i = 0 ; i < sorted_indices.size() ; i++) {
        if (i != 0 && values[sorted_indices[i]].second != values[sorted_indices[i-1]].second) {
            ++rank;
        }
        ranks[sorted_indices[i]].second = rank;
        second_rank_sum += rank;

    }
#ifdef DEBUG_ZIP_CODE_TREE
    cerr << "Ranks: " << endl;
    for (const auto& x : ranks) {
        cerr << x.first << "/" << x.second << "\t";
    }
    cerr << endl;
#endif

    double avg_first_rank = (double)first_rank_sum / (double)ranks.size();
    double avg_second_rank = (double)second_rank_sum / (double)ranks.size();
    
    double cov = 0.0;
    double sum_sq_first = 0.0;
    double sum_sq_second = 0.0;
    for (const auto& rank_tuple : ranks) {
        cov += (((double)rank_tuple.first - avg_first_rank) 
                * ((double)rank_tuple.second - avg_second_rank));

        sum_sq_first += ((double)rank_tuple.first - avg_first_rank) 
                         * ((double)rank_tuple.first - avg_first_rank);
        sum_sq_second += ((double)rank_tuple.second - avg_second_rank) 
                         * ((double)rank_tuple.second - avg_second_rank);
    }

    cov = ranks.size()==0 ? 0.0 : cov / ranks.size();
    
    double stddev_first = ranks.size()==0 ? 0 : std::sqrt(sum_sq_first / ranks.size());
    double stddev_second = ranks.size()==0 ? 0 : std::sqrt(sum_sq_second / ranks.size());
    double correlation = stddev_first==0 || stddev_second == 0 || ranks.size() == 0 
                         ? 0.0
                         : cov / (stddev_first * stddev_second);
#ifdef DEBUG_ZIP_CODE_TREE 
    cerr << "Correlation: " << correlation << endl;
#endif

    return correlation;

}


std::pair<size_t, size_t> ZipCodeTree::dag_and_non_dag_snarl_count(const vector<Seed>& seeds, 
                                                                   const SnarlDistanceIndex& distance_index) const {
    size_t dag_count = 0;
    size_t non_dag_count = 0;

    /* Walk through everything in the zip code tree and at the first seed in each snarl, 
       check if it is a dag or not
    */

    //Keep track of the depth to check the zip codes
    size_t current_depth = 0;

    //When we encounter the start of a snarl, make a note of the depth. At the next seed,
    //check the snarls at the depths recorded
    vector<size_t> snarl_depths;

    for (size_t i = 0 ; i < zip_code_tree.size() ; i++ ) {
        const tree_item_t& current_item = zip_code_tree[i];
        if (current_item.get_type() == ZipCodeTree::SNARL_START) {
            //For the start of a snarl, make a note of the depth to check the next seed
            snarl_depths.emplace_back(current_depth);

            //Increment the depth
            current_depth++;
        } else if (current_item.get_type() == ZipCodeTree::CHAIN_START) {
            //For the start of a chain, increment the depth
            current_depth++;
        } else if (current_item.get_type() == ZipCodeTree::CHAIN_END 
                    || current_item.get_type() == ZipCodeTree::SNARL_END) {
            //For the end of a snarl or chain, decrement the depth
            current_depth--;
        } else if (current_item.get_type() == ZipCodeTree::SEED) {
            //If this is a seed, check the snarls we've seen previously
            for (const size_t& snarl_depth : snarl_depths) {
                if (seeds[current_item.get_value()].zipcode_decoder->get_code_type(snarl_depth) 
                        == ZipCode::REGULAR_SNARL) {
                    //If this is a regular snarl, then it must be a DAG too
                    dag_count++;
                } else {
                    //If this is an irregular snarl

                    //Check the snarl in the distance index
                    net_handle_t snarl_handle = seeds[current_item.get_value()].zipcode_decoder->get_net_handle(snarl_depth, &distance_index);
#ifdef DEBUG_ZIP_CODE_TREE
                    assert(seeds[current_item.get_value()].zipcode_decoder->get_code_type(snarl_depth) == ZipCode::IRREGULAR_SNARL ||
                           seeds[current_item.get_value()].zipcode_decoder->get_code_type(snarl_depth) == ZipCode::CYCLIC_SNARL ||
                           seeds[current_item.get_value()].zipcode_decoder->get_code_type(snarl_depth) == ZipCode::ROOT_SNARL);
                    assert(distance_index.is_snarl(snarl_handle));
#endif
                    if (distance_index.is_dag(snarl_handle)) {
                        dag_count++;
                    } else {
                        non_dag_count++;
#ifdef PRINT_NON_DAG_SNARLS
                        size_t child_count = 0;
                        distance_index.for_each_child(snarl_handle, [&](const net_handle_t& child) {
                            child_count++;
                        });
                        cerr << distance_index.net_handle_as_string(snarl_handle) << "\t" << child_count << endl;
#endif
                    }
                }

            }
            //Clear the snarls
            snarl_depths.clear();
        }
    }

    return std::make_pair(dag_count, non_dag_count);
}
bool ZipCodeTree::seed_is_reversed_at_depth (const Seed& seed, size_t depth, const SnarlDistanceIndex& distance_index){
    if (seed.zipcode_decoder->get_is_reversed_in_parent(depth)) {
        return true;
    } else if (depth > 0 && (seed.zipcode_decoder->get_code_type(depth-1) == ZipCode::IRREGULAR_SNARL
                             || seed.zipcode_decoder->get_code_type(depth-1) == ZipCode::CYCLIC_SNARL)) {
        //If the parent is an irregular snarl, then check the orientation of the child in the snarl
        net_handle_t snarl_handle = seed.zipcode_decoder->get_net_handle(depth-1, &distance_index);
        size_t rank = seed.zipcode_decoder->get_rank_in_snarl(depth);
        if (distance_index.distance_in_snarl(snarl_handle, 0, false, rank, false)
                    == std::numeric_limits<size_t>::max()
            &&
            distance_index.distance_in_snarl(snarl_handle, 1, false, rank, true)
                    == std::numeric_limits<size_t>::max()) {
            //If the distance from the start of the snarl to the start of the child is infinite
            //and the distance from the end of the snarl to the end of the child is infinite
            //then we assume that this child is "reversed" in the parent snarl
            return true;
        } else {
            return false;
        }
    } else {
        return false;
    }
}


void ZipCodeTree::print_self(const vector<Seed>* seeds) const {
    for (const tree_item_t item : zip_code_tree) {
        if (item.get_type() == SEED) {
            cerr << seeds->at(item.get_value()).pos << "/" << seeds->at(item.get_value()).source;
            if (item.get_is_reversed()) {
                cerr << "rev";
            }
        } else if (item.get_type() == SNARL_START) {
            cerr << "(";
        } else if (item.get_type() == SNARL_END) {
            cerr << ")";
        } else if (item.get_type() == CHAIN_START) {
            cerr << "[";
        } else if (item.get_type() == CHAIN_END) {
            cerr << "]";
        } else if (item.get_type() == EDGE) {
            cerr << " " << item.get_value() << " ";
        } else if (item.get_type() == NODE_COUNT) {
            cerr << " " << item.get_value();
        } else {
            throw std::runtime_error("[zip tree]: Trying to print a zip tree item of the wrong type");
        }
    }
    cerr << endl;
}

bool ZipCodeTree::node_is_invalid(nid_t id, const SnarlDistanceIndex& distance_index, size_t distance_limit) const {
    bool is_invalid = false;
    net_handle_t net = distance_index.get_node_net_handle(id);
    while (!distance_index.is_root(net)) {
        if (distance_index.is_multicomponent_chain(net) || distance_index.is_looping_chain(net)) {
            //If this is something that we haven't handled
            is_invalid = true;
            break;
        } else if (distance_index.is_chain(distance_index.get_parent(net)) && 
                    !distance_index.is_trivial_chain(distance_index.get_parent(net))) {
            //Check if this net_handle_t could be involved in a chain loop that is smaller than the distance limit
            size_t forward_loop = distance_index.is_node(net) 
                                ? distance_index.get_forward_loop_value(net)
                                : distance_index.get_forward_loop_value(
                                        distance_index.get_node_from_sentinel(distance_index.get_bound(net, true, false)));
            size_t reverse_loop = distance_index.is_node(net) 
                                ? distance_index.get_reverse_loop_value(net)
                                : distance_index.get_reverse_loop_value(    
                                     distance_index.get_node_from_sentinel(distance_index.get_bound(net, false, false)));
            if (forward_loop < distance_limit ||
                reverse_loop < distance_limit) {
                is_invalid = true;
                break;
            }
        }
        net = distance_index.get_parent(net);
    }
    if (distance_index.is_root_snarl(net)) {
        is_invalid = true;
    }
                
    return is_invalid;
}

bool ZipCodeTree::node_is_in_cyclic_snarl(nid_t id, const SnarlDistanceIndex& distance_index) const {
    bool is_cyclic_snarl = false;
    net_handle_t net = distance_index.get_node_net_handle(id);
    while (!distance_index.is_root(net)) {
        if (distance_index.is_snarl(net) && !distance_index.is_dag(net)) {
            //If this is a cyclic snarl
            is_cyclic_snarl = true;;
            break;
        }
        net = distance_index.get_parent(net);
    }
    return is_cyclic_snarl;
}

void ZipCodeTree::validate_zip_tree(const SnarlDistanceIndex& distance_index, 
                                    const vector<Seed>* seeds,
                                    size_t distance_limit) const {
#ifdef DEBUG_ZIP_CODE_TREE
    cerr << "Validate tree with distance limit " << distance_limit << endl;
#endif

    assert(zip_code_tree.size() != 0);

    /**********  Make sure that all snarls/chains are opened and closed in a valid order ****************/
    vector<tree_item_type_t> snarl_stack; 
    for (size_t i = 0 ; i < zip_code_tree.size() ; i++) {
        const tree_item_t& item = zip_code_tree[i];
        if (item.get_type() == SNARL_START) {
            if (!snarl_stack.empty()) {
                //ALso check snarl distances and child count for non-root snarls
                validate_snarl(zip_code_tree.begin() + i, distance_index, seeds, distance_limit); 
            }
            snarl_stack.push_back(SNARL_START);
        } else if (item.get_type() == CHAIN_START) {
            snarl_stack.push_back(CHAIN_START);
        } else if (item.get_type() == SNARL_END) {
            assert(snarl_stack.back() == SNARL_START);
            snarl_stack.pop_back();
        } else if (item.get_type() == CHAIN_END) {
            assert(snarl_stack.back() == CHAIN_START);
            snarl_stack.pop_back();
        }
    }

    /************ Make sure that everything is in a valid order ****************/
    size_t previous_seed_index = std::numeric_limits<size_t>::max();
    bool previous_is_invalid = false;
    for (size_t i = 0 ; i < zip_code_tree.size() ; i++) {
        const tree_item_t& current_item = zip_code_tree[i];
        if (current_item.get_type() == SEED) {
            //Check if this is worth validating
            //Use a distance limit of 0 so it will ignore looping chains
            bool current_is_invalid = node_is_invalid(id(seeds->at(current_item.get_value()).pos), distance_index, 0);
            bool current_is_in_cyclic_snarl = node_is_in_cyclic_snarl(id(seeds->at(current_item.get_value()).pos), 
                                                                      distance_index);

            if (previous_seed_index != std::numeric_limits<size_t>::max() &&
                !current_is_invalid && !previous_is_invalid) {
                assert(previous_seed_index < seeds->size());
                assert(current_item.get_value() < seeds->size());
#ifdef DEBUG_ZIP_CODE_TREE
                cerr << "Comparing seeds " << seeds->at(previous_seed_index).pos << " and " 
                     << seeds->at(current_item.get_value()).pos << endl;
#endif

                //Comparator returning previous_seed_index < current_item.value
                size_t depth = 0;

                //Keep track of the orientation of each seed
                //Everything should be sorted according to the orientation in the top-level structure,
                //so if things are traversed backwards, reverse the orientation
                bool a_is_reversed = false;
                bool b_is_reversed = false;
                while (depth < seeds->at(previous_seed_index).zipcode_decoder->max_depth() &&
                       depth < seeds->at(current_item.get_value()).zipcode_decoder->max_depth() &&
                       ZipCodeDecoder::is_equal(*seeds->at(previous_seed_index).zipcode_decoder, 
                                                *seeds->at(current_item.get_value()).zipcode_decoder, depth)) {

                    //Remember the orientation
                    if (ZipCodeTree::seed_is_reversed_at_depth(seeds->at(previous_seed_index), depth, distance_index)) { 
                        a_is_reversed = !a_is_reversed;
                    }
                    if (ZipCodeTree::seed_is_reversed_at_depth(seeds->at(current_item.get_value()), depth, distance_index)) {
                        b_is_reversed = !b_is_reversed;
                    }

                    depth++;
                }

                //Remember the orientation of the parent too
                size_t parent_of_a_is_reversed = a_is_reversed;

                //Check the orientations one last time
                if (ZipCodeTree::seed_is_reversed_at_depth(seeds->at(previous_seed_index), depth, distance_index)) { 
                    a_is_reversed = !a_is_reversed;
                }
                if (ZipCodeTree::seed_is_reversed_at_depth(seeds->at(current_item.get_value()), depth, distance_index)) {
                    b_is_reversed = !b_is_reversed;
                }
                
#ifdef DEBUG_ZIP_CODE_TREE
                cerr << "\t different at depth " << depth << endl;
#endif
                //Either depth is the last thing in previous_seed_index or current_item.value, or they are different at this depth


                if ( ZipCodeDecoder::is_equal(*seeds->at(previous_seed_index).zipcode_decoder, 
                                              *seeds->at(current_item.get_value()).zipcode_decoder, depth)) {
#ifdef DEBUG_ZIP_CODE_TREE
                    cerr << "\tthey are on the same node" << endl;
#endif
                    //If they are equal, then they must be on the same node

                    size_t offset1 = is_rev(seeds->at(previous_seed_index).pos)
                                   ? seeds->at(previous_seed_index).zipcode_decoder->get_length(depth) 
                                        - offset(seeds->at(previous_seed_index).pos)
                                   : offset(seeds->at(previous_seed_index).pos);
                    size_t offset2 = is_rev(seeds->at(current_item.get_value()).pos)
                                   ? seeds->at(current_item.get_value()).zipcode_decoder->get_length(depth) 
                                        - offset(seeds->at(current_item.get_value()).pos)
                                   : offset(seeds->at(current_item.get_value()).pos);
                    if (!current_is_in_cyclic_snarl) {
                        if (!a_is_reversed) {
                            //If they are in previous_seed_index snarl or they are facing forward on a chain, then order by
                            //the offset in the node
                            assert( offset1 <= offset2);
                        } else {
                            //Otherwise, the node is facing backwards in the chain, so order backwards in node
                            assert( offset2 <= offset1);
                        }
                    }
                }  else if (depth == 0) {
#ifdef DEBUG_ZIP_CODE_TREE
                    cerr << "\tThey are on different connected components" << endl;
#endif
                    //If they are on different connected components, sort by connected component
                    assert( seeds->at(previous_seed_index).zipcode_decoder->get_distance_index_address(0) <= 
                            seeds->at(current_item.get_value()).zipcode_decoder->get_distance_index_address(0));
                    
                }  else if (seeds->at(previous_seed_index).zipcode_decoder->get_code_type(depth-1) == ZipCode::CHAIN 
                            || seeds->at(previous_seed_index).zipcode_decoder->get_code_type(depth-1) == ZipCode::ROOT_CHAIN) {
#ifdef DEBUG_ZIP_CODE_TREE
                    cerr << "\t they are children of a common chain" << endl;
#endif
                    //If previous_seed_index and current_item.value are both children of a chain
                    size_t offset_a = seeds->at(previous_seed_index).zipcode_decoder->get_offset_in_chain(depth);
                    size_t offset_b = seeds->at(current_item.get_value()).zipcode_decoder->get_offset_in_chain(depth);
                    if (!current_is_in_cyclic_snarl) {

                        if ( offset_a == offset_b) {
                            //If they have the same prefix sum, then the snarl comes first
                            //They will never be on the same child at this depth
                            if (parent_of_a_is_reversed) {
                                assert(seeds->at(current_item.get_value()).zipcode_decoder->get_code_type(depth) != ZipCode::NODE && 
                                       seeds->at(previous_seed_index).zipcode_decoder->get_code_type(depth) == ZipCode::NODE); 
                            } else {
                                assert( seeds->at(previous_seed_index).zipcode_decoder->get_code_type(depth) != ZipCode::NODE && 
                                        seeds->at(current_item.get_value()).zipcode_decoder->get_code_type(depth) == ZipCode::NODE); 
                            }
                        } else {
                            //Check if the parent chain is reversed and if so, then the order should be reversed
                            //The parent could be reversed if it is in an irregular snarl and the 
                            if (parent_of_a_is_reversed) {
                                assert( offset_b <= offset_a);
                            } else {
                                assert( offset_a <= offset_b);
                            }
                        }
                    }
                } else if (seeds->at(previous_seed_index).zipcode_decoder->get_code_type(depth-1) == ZipCode::REGULAR_SNARL
                            || seeds->at(previous_seed_index).zipcode_decoder->get_code_type(depth-1) == ZipCode::IRREGULAR_SNARL) {
#ifdef DEBUG_ZIP_CODE_TREE
                    cerr << "\t they are children of a common dag snarl" << endl;
#endif
                    // Otherwise, they are children of a snarl
                    // Sort by a topological ordering from the start of the snarl
                    // The ranks of children in snarls are in a topological order, so 
                    // sort on the ranks
                    if (!current_is_in_cyclic_snarl) {
                        assert( seeds->at(previous_seed_index).zipcode_decoder->get_rank_in_snarl(depth) <=
                                seeds->at(current_item.get_value()).zipcode_decoder->get_rank_in_snarl(depth));
                    }
                } 

            }
            previous_seed_index = current_item.get_value();
            previous_is_invalid = current_is_invalid;
        } else if (current_item.get_type() == CHAIN_START) {
            //Chains can't start with edges
            assert(zip_code_tree[i+1].get_type() != EDGE);
        } else if (current_item.get_type() == CHAIN_END) {
            //And can't end with edges
            assert(zip_code_tree[i-1].get_type() != EDGE);
        }
    }



    /************* Check distances and snarl tree relationships *******************/

    //Start from the end of the zip tree and walk left, checking each pair of seeds
    for (auto start_itr_left  = zip_code_tree.rbegin() ; 
         start_itr_left != zip_code_tree.rend() ; ++ start_itr_left ) {

        //Get a reverse iterator to the vector, starting from the end and going left
        if (start_itr_left->get_type() != SEED) {
            continue;
        }

        //The seed that the iterator points to
        const Seed& start_seed = seeds->at(start_itr_left->get_value());

        //Do we want the distance going left in the node
        //This takes into account the position and the orientation of the tree traversal
        bool start_is_reversed = start_itr_left->get_is_reversed() ? !is_rev(start_seed.pos) 
                                                                   : is_rev(start_seed.pos);

        //For cyclic snarls, the tree distance isn't always guaranteed to be the same as the minimum distance
        // I think that the smallest distance between any pair of seeds will be guaranteed to be the same as the
        // actual minimum distance, so store the minimum (non infinite) distance here
        // The first pair of size_t's are indices into seeds (start then next), 
        // the second pair are the tree distance and actual distance

        //Walk through the tree starting from the vector iterator going left, and check the distance
        for (reverse_iterator tree_itr_left (start_itr_left, zip_code_tree.rend()) ;
             tree_itr_left != reverse_iterator(zip_code_tree.rend(), zip_code_tree.rend()) ;
             ++tree_itr_left) {

            seed_result_t next_seed_result = *tree_itr_left;
            const Seed& next_seed = seeds->at(next_seed_result.seed);
            const bool next_is_reversed = next_seed_result.is_reverse ? !is_rev(next_seed.pos) 
                                                                      : is_rev(next_seed.pos);

            size_t tree_distance = next_seed_result.distance;

            net_handle_t start_handle = distance_index.get_node_net_handle(
                                            id(start_seed.pos),
                                            is_rev(start_seed.pos) != start_is_reversed);
            net_handle_t next_handle = distance_index.get_node_net_handle(
                                            id(next_seed.pos),
                                            is_rev(next_seed.pos) != next_is_reversed);

            size_t index_distance = distance_index.minimum_distance(id(next_seed.pos), is_rev(next_seed.pos), offset(next_seed.pos),
                    id(start_seed.pos), is_rev(start_seed.pos), offset(start_seed.pos), true);

            if (index_distance != std::numeric_limits<size_t>::max() && is_rev(next_seed.pos) != next_is_reversed) {
                //If the seed we're starting from got reversed, then subtract 1
                index_distance -= 1;
            }
            if (index_distance != std::numeric_limits<size_t>::max() && is_rev(start_seed.pos) != start_is_reversed) {
                //If the seed we ended at got reversed, then add 1
                index_distance += 1;
            }
            pos_t start_pos = is_rev(start_seed.pos) 
                            ? make_pos_t(id(start_seed.pos), 
                                         false, 
                                         distance_index.minimum_length(start_handle) - offset(start_seed.pos) )
                            : start_seed.pos;
            pos_t next_pos = is_rev(next_seed.pos) 
                           ? make_pos_t(id(next_seed.pos), 
                                        false, 
                                        distance_index.minimum_length(next_handle) - offset(next_seed.pos) )
                           : next_seed.pos;
            size_t start_length = distance_index.minimum_length(start_handle);
            size_t next_length = distance_index.minimum_length(next_handle);

            bool in_non_dag_snarl = node_is_in_cyclic_snarl(id(next_seed.pos), distance_index) ||
                                    node_is_in_cyclic_snarl(id(start_seed.pos), distance_index);

            bool distance_is_invalid = node_is_invalid(id(next_seed.pos), distance_index, distance_limit) ||
                               node_is_invalid(id(start_seed.pos), distance_index, distance_limit);

            if (in_non_dag_snarl) {
                //TODO: I don't actually know how to check these properly

            } else if (!distance_is_invalid && index_distance <= distance_limit) {
                if (start_pos == next_pos) {
                    if (tree_distance != 0 && tree_distance != index_distance) {
                        for (auto& seed : *seeds) {
                            cerr << seed.pos << endl;
                        }
                        cerr << "Distance between " << next_seed.pos << (next_is_reversed ? "rev" : "") 
                             << " and " << start_seed.pos << (start_is_reversed ? "rev" : "") << endl;
                        cerr << "Forward positions: " << start_pos << " " << next_pos << " and length " << start_length << endl;
                        cerr << "Tree distance: " << tree_distance << " index distance: " << index_distance << endl;
                        cerr << "With distance limit: " << distance_limit << endl;
                    }
                    //This could be off by one if one of the seeds is reversed, but I'm being lazy and just checking against the index
                    assert((tree_distance == 0 || tree_distance == index_distance));
                } else {
                    if (tree_distance != index_distance) {
                        for (auto& seed : *seeds) {
                            cerr << seed.pos << endl;
                        }
                        cerr << "Distance between " << next_seed.pos << (next_is_reversed ? "rev" : "") 
                             << " and " << start_seed.pos << (start_is_reversed ? "rev" : "") << endl;
                        cerr << "Forward positions: " << start_pos << " " << next_pos << " and lengths " 
                             << start_length << " " << next_length << endl;
                        cerr << "Tree distance: " << tree_distance << " index distance: " << index_distance << endl;
                        cerr << "With distance limit: " << distance_limit << endl;
                    }
                    assert(tree_distance == index_distance);
                }
            }

        }

    }
}
void ZipCodeForest::validate_zip_forest(const SnarlDistanceIndex& distance_index, 
                                        const vector<Seed>* seeds, size_t distance_limit) const {
    vector<bool> has_seed (seeds->size(), false);
    for (const auto& tree : trees) {
        tree.validate_zip_tree(distance_index, seeds, distance_limit);
        for (size_t i = 0 ; i < tree.zip_code_tree.size() ; i++) {
            const tree_item_t& item = tree.zip_code_tree[i];
            if (item.get_type() == ZipCodeTree::SEED) {
                has_seed[item.get_value()] = true;
            }
        }
    }

    for (size_t i = 0 ; i < has_seed.size() ; i++) {
        bool x = has_seed[i];
        if (!x) { cerr << "Missing seed " << seeds->at(i).pos << endl;}
        assert(x);
    }
}



//Helper function for validating a snarl. zip_iterator is an iterator to the snarl start
void ZipCodeTree::validate_snarl(std::vector<tree_item_t>::const_iterator zip_iterator, 
                                 const SnarlDistanceIndex& distance_index, 
                                 const vector<Seed>* seeds,
                                 size_t distance_limit) const {

    //For checking distances, remember the last seed in each chain. 
    //For snarls at the end of chains, store a position with node id 0
    //to ignore it because I don't know how to check that 
    vector<pos_t> from_positions;

    //Distances come before the chain that they end at, so build up a 
    //vector of distances to check when we reach the chain
    vector<size_t> distances;

    //Start with the snarl start TODO: Actually do this
    from_positions.emplace_back(make_pos_t(0, false, 0));
    zip_iterator++;
    while (zip_iterator->get_type() != NODE_COUNT) {
        if (zip_iterator->get_type() == EDGE) {
            distances.emplace_back(zip_iterator->get_value());
            zip_iterator++;
        } else if (zip_iterator->get_type() == CHAIN_START) {
            //If this is the start of a chain, check distances and get to the
            //end of the chain

            //If the chain starts on a seed, then check the distances. Otherwise, 
            // it must be a snarl and we can't check distances
            zip_iterator++;
            if (zip_iterator->get_type() == SNARL_START) {
                //Just validate the nested snarl
                validate_snarl(zip_iterator, distance_index, seeds, distance_limit);
            } else if (zip_iterator->get_type() == SEED) {
                //Check distances from all children before the seed to the seed
                assert(distances.size() == from_positions.size());
                pos_t to_pos = seeds->at(zip_iterator->get_value()).pos; 
                if (zip_iterator->get_is_reversed()) {
                    to_pos = make_pos_t(id(to_pos),
                                          !is_rev(to_pos),
                                          distance_index.minimum_length(
                                                distance_index.get_node_net_handle(id(to_pos)))
                                              - offset(to_pos));
                }
                for (size_t i = 0 ; i < distances.size() ; i ++) {
                    pos_t from_pos = from_positions[from_positions.size() - 1 - i];
                    if (id(from_pos) != 0) {
                        size_t distance = minimum_distance(distance_index, from_pos, to_pos);
#ifdef DEBUG_ZIP_CODE_TREE
                        cerr << "Distance between " << from_pos << " and " << to_pos << " is " << distance 
                             << " guessed: " << distances[i] << endl;
#endif
                        if (from_pos == to_pos) {
                            //TODO: This should check for loops but i'll do that later
                        } else if (node_is_invalid(id(to_pos), distance_index, distance_limit) ||
                                   node_is_invalid(id(from_pos), distance_index, distance_limit) ) {
                            //If the minimum distances uses a loop on a chain
                        } else if (distance < distance_limit) {
                            assert(distance == distances[i]);
                        } else {
                            assert(distances[i] >= distance_limit);
                        }
                    }
                    
                }
            }
            //Now get to the end of the chain
            //Make sure we find the correct chain_end by remembering how many we opened
            size_t open_chain_count = 1;
            while (open_chain_count > 0) {
                if (zip_iterator->get_type() == CHAIN_START) {
                    open_chain_count++;
                } else if (zip_iterator->get_type() == CHAIN_END) {
                    open_chain_count--;
                }
                zip_iterator++;
            }
            //zip_iterator now points to one thing after the end of the child chain
            // If the last thing in the chain was a node, add the position, otherwise
            //add an empty position
            auto last = zip_iterator-2;
            if (last->get_type() == SEED) {
                //The last seed pointing out
                pos_t from_pos = seeds->at(last->get_value()).pos;
                if (last->get_is_reversed()) {
                    from_pos = make_pos_t(id(from_pos),
                                          !is_rev(from_pos),
                                          distance_index.minimum_length(
                                                distance_index.get_node_net_handle(id(from_pos)))
                                              - offset(from_pos));
                }
                from_positions.emplace_back(from_pos);
            } else {
                from_positions.emplace_back(make_pos_t(0, false, 0));
            }

            //Clear the list of distances
            distances.clear();
        } else {
            assert(zip_iterator->get_type() == NODE_COUNT);
            zip_iterator++;
        }

    }
    //TODO: Check the distances to the end of the snarl

    //zip_iterator now points to the node count
    assert(from_positions.size()-1 == zip_iterator->get_value());
    zip_iterator++;
    assert(zip_iterator->get_type() == SNARL_END);
    return;
};



ZipCodeTree::iterator::iterator(vector<tree_item_t>::const_iterator begin, vector<tree_item_t>::const_iterator end) : it(begin), end(end) {
    while (this->it != this->end && this->it->get_type() != SEED) {
        // Immediately advance to the first seed
        ++this->it;
    }
}

auto ZipCodeTree::iterator::operator++() -> iterator& {
    ++it;
    while (it != end && it->get_type() != SEED) {
        // Advance to the next seed, or the end.
        ++it;
    }
    return *this;
}

auto ZipCodeTree::iterator::operator==(const iterator& other) const -> bool {
    // Ends don't matter for comparison.
    return it == other.it;
}
    
auto ZipCodeTree::iterator::operator*() const -> oriented_seed_t {
    return {it->get_value(), it->get_is_reversed()};
}

auto ZipCodeTree::iterator::remaining_tree() const -> size_t {
    size_t to_return = end - it - 1;
#ifdef debug_parse
    std::cerr << "From " << &*it << " there are " << to_return << " slots after" << std::endl;
#endif
    return to_return;
}

auto ZipCodeTree::begin() const -> iterator {
    return iterator(zip_code_tree.begin(), zip_code_tree.end());
}

auto ZipCodeTree::end() const -> iterator {
    return iterator(zip_code_tree.end(), zip_code_tree.end());
}

ZipCodeTree::reverse_iterator::reverse_iterator(vector<tree_item_t>::const_reverse_iterator rbegin, vector<tree_item_t>::const_reverse_iterator rend, size_t distance_limit) : it(rbegin), rend(rend), distance_limit(distance_limit), stack(), current_state(S_START) {
#ifdef debug_parse
    if (this->it != rend) {
        std::cerr << "Able to do first initial tick." << std::endl;
    }
#endif
    if (this->it == rend) {
        // We are an end iterator. Nothing else to do.
        return;
    }
    while (this->it != rend && !tick()) {
        // Skip ahead to the first seed we actually want to yield, or to the end of the data.
        ++this->it;
#ifdef debug_parse
        if (this->it != rend) {
            std::cerr << "Able to do another initial tick." << std::endl;
        }
#endif
    }
    // As the end of the constructor, the iterator points to a seed that has been ticked and yielded, or is rend.
#ifdef debug_parse
    if (this->it == rend) {
        std::cerr << "Ran out of tree looking for first seed." << std::endl;
    }
#endif
}

auto ZipCodeTree::reverse_iterator::operator++() -> reverse_iterator& {
    // Invariant: the iterator points to a seed that has been ticked and yielded, or to rend.
    if (it != rend) {
#ifdef debug_parse
        std::cerr << "Skipping over a " << it->get_type() << " which we assume was handled already." << std::endl;
#endif
        ++it;

    }
    while (it != rend && !tick()) {
        // Skip ahead to the next seed we actually want to yield, or to the end of the data.
        ++it;
    }
#ifdef debug_parse
    if (it == rend) {
        std::cerr << "Ran out of tree looking for next seed." << std::endl;
    }
#endif
    return *this;
}

auto ZipCodeTree::reverse_iterator::operator==(const reverse_iterator& other) const -> bool {
    // Ends and other state don't matter for comparison.
    return it == other.it;
}

auto ZipCodeTree::reverse_iterator::operator*() const -> seed_result_t {
    // We are always at a seed, so show that seed
    crash_unless(it != rend);
    crash_unless(it->get_type() == SEED);
    crash_unless(!stack.empty());
    // We know the running distance to this seed will be at the top of the stack.
    seed_result_t to_return;
    to_return.seed = it->get_value();
    to_return.is_reverse = it->get_is_reversed();
    to_return.distance = stack.top();
    return to_return;
}

auto ZipCodeTree::reverse_iterator::push(size_t value) -> void {
    stack.push(value);
}

auto ZipCodeTree::reverse_iterator::pop() -> size_t {
    size_t value = stack.top();
    stack.pop();
    return value;
}

auto ZipCodeTree::reverse_iterator::top() -> size_t& {
    crash_unless(depth() > 0);
    return stack.top();
}

auto ZipCodeTree::reverse_iterator::dup() -> void {
    push(stack.top());
}

auto ZipCodeTree::reverse_iterator::depth() const -> size_t {
    return stack.size();
}

auto ZipCodeTree::reverse_iterator::swap() -> void {
    // Grab the top item
    size_t temp = stack.top();
    stack.pop();
    // Swap it with what was under it
    std::swap(temp, stack.top());
    // And put that back on top
    stack.push(temp);
}

auto ZipCodeTree::reverse_iterator::state(State new_state) -> void {
    current_state = new_state;
}

auto ZipCodeTree::reverse_iterator::halt() -> void {
#ifdef debug_parse
    std::cerr << "Halt iteration!" << std::endl;
#endif
    it = rend;
}

auto ZipCodeTree::reverse_iterator::tick() -> bool {
#ifdef debug_parse
    std::cerr << "Tick for state " << current_state << " on symbol " << it->get_type() << " at " << &*it << std::endl;
#endif
    switch (current_state) {
    case S_START:
        // Initial state.
        //
        // Stack is empty and we must be at a seed to start at.
        switch (it->get_type()) {
        case SEED:
#ifdef debug_parse
            std::cerr << "Skip over seed " << it->get_value() << std::endl;
#endif
            push(0);
            state(S_SCAN_CHAIN);
            break;
        default:
            throw std::domain_error("Unimplemented symbol " + std::to_string(it->get_type()) + " for state " + std::to_string(current_state)); 
        }
        break;
    case S_SCAN_CHAIN:
        // State where we are scanning a chain leftward up to its start.
        //
        // Stack has at the top the running distance along the chain, and under
        // that running distances to use at the other chains in the snarl, and
        // under that running distances to use for the other chains in the
        // snarl's parent snarl, etc.
        switch (it->get_type()) {
        case SEED:
            // Emit seed here with distance at top of stack.
            crash_unless(depth() > 0);
#ifdef debug_parse
            std::cerr << "Yield seed " << it->get_value() << ", distance " << top() << std::endl;
#endif
            return true;
            break;
        case SNARL_END:
            // Running distance along chain is on stack, and will need to be added to all the stored distances.
            state(S_STACK_SNARL); // Stack up pre-made scratch distances for all the things in the snarl.
            break;
        case CHAIN_START:
            if (depth() == 1) {
                // We never entered the parent snarl of this chain, so stack up
                // the distances left of here as options added to the
                // distance along this chain.
                //
                // Running distance along chain is on stack, and will need to
                // be added to all the stored distances.
                // Note that there may be 0 stored distances if we are below the top-level snarl.
                state(S_STACK_SNARL);
            } else {
                // We did enter the parent snarl already.
                // Discard the running distance along this chain, which no longer matters.
                pop();
                // Running distance for next chain, or running distance to cross the snarl, will be under it.
                state(S_SCAN_SNARL);
            }
            break;
        case EDGE:
            // Distance between things in a chain.
            // Add value into running distance, maxing it if value is max.
            top() = SnarlDistanceIndex::sum(top(), it->get_value());
            if (top() > distance_limit || top() == std::numeric_limits<size_t>::max()) {
                // Skip over the rest of this chain
                if (depth() == 1) {
                    // We never entered the parent snarl of this chain.
                    // So if the distance along the chain is too much, there
                    // are not going to be any results with a smaller distance.
                    halt();
                    // When we halt we have to return true to show the halting position.
                    return true;
                } else {
                    // We need to try the next thing in the parent snarl, so skip the rest of the chain.
                    // We're skipping in 0 nested snarls right now.
                    push(0);
                    state(S_SKIP_CHAIN);
                }
            }
            break;
        default:
            throw std::domain_error("Unimplemented symbol " + std::to_string(it->get_type()) + " for state " + std::to_string(current_state)); 
        }
        break;
    case S_STACK_SNARL:
        // State where we are stacking up the stored edge values, the first
        // time we get to a particular snarl.
        //
        // Stack has the running distance along the parent chain, and under
        // that the stacked running distances for items in the snarl.
        switch (it->get_type()) {
        case EDGE:
            // We need to add this actual number to parent running distance.
            // Duplicate parent running distance
            dup();
            // Add in the edge value to make a running distance for the thing this edge is for.
            // Account for if the edge is actually unreachable.
            top() = SnarlDistanceIndex::sum(top(), it->get_value());
            // Flip top 2 elements, so now parent running distance is on top, over edge running distance.
            swap();
            break;
        case CHAIN_END:
            // Throw out parent running distance
            pop();
            if (depth() == 0) {
                // We left a chain and immediately entered a chain without a distance.
                // This means the chains aren't actually connected.
                halt();
                // When we halt we have to return true to show the halting position.
                return true;
            } else {
                // So now we have the running distance for this next chain.
                if (top() > distance_limit || top() == std::numeric_limits<size_t>::max()) {
                    // Running distance is already too high so skip over the chain
                    push(0);
                    state(S_SKIP_CHAIN);
                } else {
                    // Do the chain
                    state(S_SCAN_CHAIN);
                }
            }
            break;
        case SNARL_START:
            // We didn't hit another chain in the snarl, we hit the start of
            // the snarl. We should have stacked exactly one or zero distances.
            
            if (depth() == 1) {
                // We have hit the start of a top-level snarl
#ifdef debug_parse
                std::cerr << "Hit start of top-level snarl" << std::endl;
#endif
                halt();
                // When we halt we have to return true to show the halting position.
                return true;
            }

            // Throw out parent running distance
            pop();

            // There will be a running distance on the stack still, and we
            // will continue with that in the parent chain.
            state(S_SCAN_CHAIN);
            break;
        case NODE_COUNT:
            // We've found the node count in the snarl. We don't need it, so
            // skip it.
            // TODO: Use it if skipping the snarl.
            break;
        default:
            throw std::domain_error("Unimplemented symbol " + std::to_string(it->get_type()) + " for state " + std::to_string(current_state)); 
        }
        break;
    case S_SCAN_SNARL:
        // State where we are going through a snarl and doing all its chains.
        //
        // Stack has at the top running distances to use for each chain still
        // to be visited in the snarl, and under those the same for the snarl
        // above that, etc.
        switch (it->get_type()) {
        case SNARL_START:
            // Stack holds running distance along parent chain plus edge
            // distance to cross the snarl, or running distance out of chain we
            // started in plus distance to exit the snarl.
            //
            // This is the right running distance to use for the parent chain now.
            // So go back to scanning the parent chain.
            state(S_SCAN_CHAIN);
            break;
        case CHAIN_END:
            // We've encountered a chain to look at, and the running distance
            // into the chain is already on the stack.
            if (top() > distance_limit || top() == std::numeric_limits<size_t>::max()) {
                // Running distance is already too high so skip over the chain
                push(0);
                state(S_SKIP_CHAIN);
            } else {
                // Do the chain
                state(S_SCAN_CHAIN);
            }
            break;
        case EDGE:
            // We've found edge data in the snarl, but we already know the
            // running distances to everything we will encounter, so we ignore
            // it.
            break;
        case NODE_COUNT:
            // We've found the node count in the snarl. We don't need it, so
            // skip it.
            break;
        default:
            throw std::domain_error("Unimplemented symbol " + std::to_string(it->get_type()) + " for state " + std::to_string(current_state)); 
        }
        break;
    case S_SKIP_CHAIN:
        // State where we are skipping over the rest of a chain because we hit
        // the distance limit, but we might need to do other chains in a parent
        // snarl.
        //
        // Stack has the nesting level of child snarls we are reading over
        // until we get back to the level we want to skip past the chain
        // start.
        // Under that is the running distance along the chain being skipped.
        // And under that it has the running distance for ther next thing in
        // the snarl, which had better exist or we shouldn't be trying to skip
        // the chain, we should have halted.
        switch (it->get_type()) {
        case SEED:
            // We don't emit seeds until the chain is over
            return false;
            break;
        case SNARL_START:
            // We might now be able to match chain starts again
            top() -= 1;
            break;
        case SNARL_END:
            // We can't match chain starts until we leave the snarl
            top() += 1;
            break;
        case CHAIN_START:
            if (top() == 0) {
                // Parent snarl may be a top-level snarl.
                if (depth() == 1) {
                    // We have hit the start of a top-level snarl
#ifdef debug_parse
                    std::cerr << "Hit start of top-level snarl" << std::endl;
#endif
                    halt();
                    // When we halt we have to return true to show the halting position.
                    return true;
                }

                // This is the start of the chain we were wanting to skip.
                pop();
                crash_unless(depth() >= 1);
                // Discard the running distance along this chain, which no longer matters.
                pop();
                // Running distance for next chain, or running distance to cross the snarl, will be under it.
                state(S_SCAN_SNARL);
            }
            // Otherwise this is the start of a chain inside a child snarl we are skipping over and we ignore it.
            break;
        case CHAIN_END:
            // Ignore chain ends
            break;
        case EDGE:
            // Ignore edge values
            break;
        case NODE_COUNT:
            // Ignore node counts
            // TODO: We should read these and jump along instead!
            break;
        default:
            throw std::domain_error("Unimplemented symbol " + std::to_string(it->get_type()) + " for state " + std::to_string(current_state)); 
        }
        break;
    default:
        throw std::domain_error("Unimplemented state " + std::to_string(current_state)); 
    }
    // Unless we yield something, we don't want to pause the scan here.
    return false;
}

auto ZipCodeTree::look_back(const iterator& from, size_t distance_limit) const -> reverse_iterator {
    return reverse_iterator(zip_code_tree.rbegin() + from.remaining_tree(), zip_code_tree.rend(), distance_limit);
}
auto ZipCodeTree::rend() const -> reverse_iterator {
    return reverse_iterator(zip_code_tree.rend(), zip_code_tree.rend(), 0);
}


std::ostream& operator<<(std::ostream& out, const ZipCodeTree::tree_item_type_t& type) {
    return out << std::to_string(type);
}

std::ostream& operator<<(std::ostream& out, const ZipCodeTree::reverse_iterator::State& state) {
    return out << std::to_string(state);
}

void ZipCodeForest::sort_one_interval(forest_growing_state_t& forest_state, 
    const interval_and_orientation_t& interval, size_t interval_depth) const {

    vector<size_t>& zipcode_sort_order = forest_state.seed_sort_order; 
    vector<sort_value_t>& sort_values_by_seed = forest_state.sort_values_by_seed;
    const vector<Seed>* seeds = forest_state.seeds;

#ifdef DEBUG_ZIP_CODE_TREE
    cerr << "Sort interval at depth " << interval_depth << (interval.is_reversed ? " reversed" : "") << endl;
#endif



    /*** First, fill in sort_values_by_seed for the relevant seeds ***/

    //This doesn't take into account the orientation, except for nodes offsets in chains
    //Used for sorting at the given depth, so use values at depth depth+1

    //Get the minimum and maximum values that are used for sorting. These will be used to determine if 
    //radix sort will be more efficient

    size_t max_sort_value = 0;
    size_t min_sort_value = std::numeric_limits<size_t>::max();


    //If this interval is a chain or node and it is being traversed backwards, save the prefix sum values facing backwards
    //If it is a root chain or node, it won't be reversed anyway
    bool order_is_reversed = interval.is_reversed && (interval.code_type == ZipCode::CHAIN || interval.code_type == ZipCode::NODE);

    for (size_t i = interval.interval_start ; i < interval.interval_end ; i++) {
        const Seed& seed = seeds->at(zipcode_sort_order[i]); 
#ifdef DEBUG_ZIP_CODE_SORTING
        cerr << "\tGet the sort value of seed " << seed.pos << " at depth " << interval_depth+1 
             << " with parent type " << interval.code_type << endl;
#endif
        if (interval.code_type == ZipCode::EMPTY) {
            // If we are sorting the root int connected components 

#ifdef DEBUG_ZIP_CODE_SORTING
            cerr << "\t\tThis is the root snarl so sort by connected component: " 
                 << seed.zipcode_decoder->get_distance_index_address(0) << endl;
#endif
            sort_values_by_seed[zipcode_sort_order[i]].set_sort_value( seed.zipcode_decoder->get_distance_index_address(0));
            sort_values_by_seed[zipcode_sort_order[i]].set_code_type(seed.zipcode_decoder->get_code_type(0));
        } else if (interval.code_type == ZipCode::NODE || interval.code_type == ZipCode::ROOT_NODE 
                    || seed.zipcode_decoder->max_depth() == interval_depth) {

#ifdef DEBUG_ZIP_CODE_SORTING
            cerr << "\t\t this is a node: offset: " << ( is_rev(seed.pos) 
                                                         ? seed.zipcode_decoder->get_length(interval_depth) - offset(seed.pos)
                                                         : offset(seed.pos)) << endl;;
#endif
            sort_values_by_seed[zipcode_sort_order[i]].set_sort_value(
                       is_rev(seed.pos) != order_is_reversed ? seed.zipcode_decoder->get_length(interval_depth) - offset(seed.pos)
                                                             : offset(seed.pos));
            sort_values_by_seed[zipcode_sort_order[i]].set_code_type(ZipCode::NODE);

        } else if (interval.code_type == ZipCode::CHAIN || interval.code_type == ZipCode::ROOT_CHAIN) {

#ifdef DEBUG_ZIP_CODE_SORTING
            cerr << "\t\t this is a chain:";
#endif
            // Get the prefix sum and chain order of the chain child. The chain order is the value added to the prefix
            // sum to specify the order of children with the same prefix sum.  1 will be added to snarls,
            // nd 2 will be added to the node with an offset in the node of 0 (node 3 if the chain is traversed forward)

            size_t prefix_sum = order_is_reversed ? SnarlDistanceIndex::minus(seed.zipcode_decoder->get_length(interval_depth),
                                                        SnarlDistanceIndex::sum( seed.zipcode_decoder->get_offset_in_chain(interval_depth+1),
                                                                                 seed.zipcode_decoder->get_length(interval_depth+1)))
                                               : seed.zipcode_decoder->get_offset_in_chain(interval_depth+1);

            ZipCode::code_type_t child_type = seed.zipcode_decoder->get_code_type(interval_depth+1);
            sort_values_by_seed[zipcode_sort_order[i]].set_code_type(child_type);

            if (child_type == ZipCode::REGULAR_SNARL 
                || child_type == ZipCode::IRREGULAR_SNARL
                || child_type == ZipCode::CYCLIC_SNARL) { 

                //For a snarl, the order is prefix_sum*3+1
                sort_values_by_seed[zipcode_sort_order[i]].set_sort_value(prefix_sum);
                sort_values_by_seed[zipcode_sort_order[i]].set_chain_order(1);
            } else {
                //If this is a node, then the offset in the position to the prefix sum
                bool node_is_rev = seed.zipcode_decoder->get_is_reversed_in_parent(interval_depth+1) != is_rev(seed.pos);
                node_is_rev = order_is_reversed ? !node_is_rev : node_is_rev;
                size_t node_offset = node_is_rev ? seed.zipcode_decoder->get_length(interval_depth+1) - offset(seed.pos)
                                                 : offset(seed.pos);

                sort_values_by_seed[zipcode_sort_order[i]].set_sort_value(SnarlDistanceIndex::sum(prefix_sum, node_offset));
                if (node_offset == 0) {
                    sort_values_by_seed[zipcode_sort_order[i]].set_chain_order(2);
                } else {
                    sort_values_by_seed[zipcode_sort_order[i]].set_chain_order(0);
                }
            }
#ifdef DEBUG_ZIP_CODE_SORTING
            cerr << "Prefix sum " << sort_values_by_seed[zipcode_sort_order[i]].get_distance_value() << " and sort value " 
                 << sort_values_by_seed[zipcode_sort_order[i]].get_sort_value() << " and type " << child_type << endl;
#endif
        } else {
#ifdef DEBUG_ZIP_CODE_SORTING
            cerr << "\tThis is snarl, so return the rank in the snarl: " << seed.zipcode_decoder->get_rank_in_snarl(interval_depth+1) << endl;
#endif
            // The ranks of children in irregular snarls are in a topological order, so 
            // sort on the ranks
            // The rank of children in a regular snarl is arbitrary but it doesn't matter anyway
            sort_values_by_seed[zipcode_sort_order[i]].set_sort_value(seed.zipcode_decoder->get_rank_in_snarl(interval_depth+1));
            sort_values_by_seed[zipcode_sort_order[i]].set_code_type(seed.zipcode_decoder->get_code_type(interval_depth+1));
        }
        min_sort_value = std::min(min_sort_value, sort_values_by_seed[zipcode_sort_order[i]].get_sort_value());
        max_sort_value = std::max(max_sort_value, sort_values_by_seed[zipcode_sort_order[i]].get_sort_value());
    }

    // If everything is already sorted, we can stop here 

    //Check if the interval is already sorted or needs to be reversed
    if (interval.is_ordered) {
        //The interval is already sorted so do nothing
#ifdef DEBUG_ZIP_CODE_TREE
        cerr << "\tTHe interval is already sorted" << endl;
#endif
        return;
    } else if (interval.is_reverse_ordered) {
        //Reverse the order. Get the order in reverse and fill it back in
        vector<size_t> order_reversed(interval.interval_end-interval.interval_start);
        for (size_t i = 0 ; i < order_reversed.size() ; i++) {
            order_reversed[i] = zipcode_sort_order[interval.interval_end-1-i];
        }
        for (size_t i = 0 ; i < order_reversed.size() ; i++) {
            zipcode_sort_order[interval.interval_start+i] = order_reversed[i];
        }
#ifdef DEBUG_ZIP_CODE_TREE
        cerr << "\tThe interval was reversed. New order:" << endl;
        for (size_t i = interval.interval_start ; i < interval.interval_end ; i++) {
            cerr << seeds->at(zipcode_sort_order[i]).pos << " ";
        }
        cerr << endl;

#endif
        return;
    }


    /***** Figure out which sort method we should use ***/

    bool use_radix;
    if (interval.code_type  == ZipCode::ROOT_CHAIN) {
        //If this is a root chain, then use the default sort, because it's probably too big for radix and we can't tell
        //anyways because we don't store the length of a root-chain
        use_radix = false;
    } else {
        //The cost of default sort is nlog(n) where n is the number of things to sort
        size_t default_cost = (interval.interval_end - interval.interval_start) 
                            * std::log2(interval.interval_end - interval.interval_start);
        //The cost of radix sort is linear in the number of distinct values (since we will subtract the minimum)
        size_t radix_cost = max_sort_value - min_sort_value;
        use_radix = radix_cost <= default_cost;
    }

    /**** Sort *********/


    //Snarls are already sorted by a topological order of the orientation of the zip tree, so don't reverse them
    //And don't reverse the sort if that has already been taken into account in the value finding
    bool reverse_order = (interval.code_type == ZipCode::REGULAR_SNARL || interval.code_type == ZipCode::IRREGULAR_SNARL || order_is_reversed) 
                         ? false
                         : interval.is_reversed; 
    if (use_radix) {
        //Sort the given interval using the value-getter and orientation
        radix_sort_zipcodes(zipcode_sort_order, sort_values_by_seed, interval, reverse_order, min_sort_value, max_sort_value);
    } else {
        //Sort the given interval using the value-getter and orientation
        default_sort_zipcodes(zipcode_sort_order, sort_values_by_seed, interval, reverse_order);
    }
    return;
}

vector<ZipCodeForest::interval_and_orientation_t> ZipCodeForest::get_next_intervals(forest_growing_state_t& forest_state, 
    const interval_and_orientation_t& interval, size_t interval_depth) const {

    vector<size_t>& zipcode_sort_order = forest_state.seed_sort_order;
    vector<sort_value_t>& sort_values_by_seed = forest_state.sort_values_by_seed;
    const vector<Seed>* seeds = forest_state.seeds;
    const SnarlDistanceIndex* distance_index = forest_state.distance_index;


    /*********   Check for new intervals of the children ****************/

    //The new intervals to return
    vector<interval_and_orientation_t> new_intervals;

#ifdef DEBUG_ZIP_CODE_TREE
    cerr << "Finding intervals after sorting at depth " << interval_depth << endl;
#endif
    //After sorting, find runs of equivalent values for new_interval_to_sort
    //Everything gets put into a new interval, even if it is the only thing with that partitioning value
    //Since nodes are really just seeds on the same chain, runs of nodes get put together even if they are
    // actually on different nodes, as long as the nodes are facing in the same direction
    //Also need to check the orientation
    //For intervals corresponding to cyclic snarls, the orientation is based on the read, not the snarl

    //max() is used for the root, when the child's depth should be 0
    size_t child_depth = interval.code_type == ZipCode::EMPTY ? 0 : interval_depth+1;


    if (interval.code_type != ZipCode::EMPTY && 
        seeds->at(zipcode_sort_order[interval.interval_start]).zipcode_decoder->max_depth() == interval_depth ) {
        //If this is a trivial chain, then just return the same interval as a node
#ifdef DEBUG_ZIP_CODE_TREE
        cerr << "\tthis was a trivial chain so just return the same interval as a node" << endl;
#endif
        new_intervals.emplace_back(interval.interval_start, interval.interval_end, interval.is_reversed, ZipCode::NODE, 
                                   child_depth);
        if (interval.is_ordered) {
            new_intervals.back().is_ordered=true;
        }
        return new_intervals;
    }


    //These get compared to see if the next seeds is in the same interval
    ZipCode::code_type_t first_type = sort_values_by_seed[zipcode_sort_order[interval.interval_start]].get_code_type();

    //This is only for nodes in chains, since anything on nodes in chains are considered just children of the chain
    bool previous_is_node = first_type == ZipCode::NODE;

    //This only matters if it isn't a node
    size_t previous_sort_value = previous_is_node 
                                   ? (ZipCodeTree::seed_is_reversed_at_depth(seeds->at(zipcode_sort_order[interval.interval_start]), 
                                                                             child_depth, *distance_index) ? 1 : 0)
                                   : sort_values_by_seed[zipcode_sort_order[interval.interval_start]].get_sort_value();

    //Start the first interval. The end value and is_reversed gets set when ending the interval
    new_intervals.emplace_back(interval.interval_start, interval.interval_start, interval.is_reversed, 
                               first_type, child_depth);

    //If the parent interval was reversed, then this is the second copy of the parent, and it was sorted and processed
    //in the forward direction already, and was reversed when sorting this interval, so it is sorted 
    if (interval.is_ordered || interval.is_reverse_ordered) {
        new_intervals.back().is_ordered=true;
    }
    for (size_t i = interval.interval_start+1 ; i < interval.interval_end ; i++) {
        
        //If the current seed is a node and has nothing at depth+1 or is different from the previous seed at this depth
        ZipCode::code_type_t current_type = sort_values_by_seed[zipcode_sort_order[i]].get_code_type();
        bool is_node = current_type == ZipCode::NODE;
        //TODO: Why is there a different sort value here?
        size_t sort_value = is_node 
                          ? (ZipCodeTree::seed_is_reversed_at_depth(seeds->at(zipcode_sort_order[i]), child_depth, *distance_index) ? 1 : 0)
                          : sort_values_by_seed[zipcode_sort_order[i]].get_sort_value();
        bool is_different_from_previous = is_node != previous_is_node ? true : sort_value != previous_sort_value;
        previous_is_node = is_node;
        previous_sort_value = sort_value;

        if (is_different_from_previous) {
            //If this is the end of a run, close the previous run
            //Add its end value and orientation

            new_intervals.back().interval_end = i;

            
            if (!previous_is_node) {
                new_intervals.back().is_reversed = ZipCodeTree::seed_is_reversed_at_depth(seeds->at(zipcode_sort_order[i-1]), 
                                                                                          child_depth, *distance_index) 
                                                 ? !interval.is_reversed
                                                 : interval.is_reversed;
            }
           
 

            //Open a new run
            new_intervals.emplace_back(i, i, interval.is_reversed, is_node ? ZipCode::NODE : current_type, 
                               child_depth);
        }
    }

    //Close the last run
    new_intervals.back().interval_end = interval.interval_end;

    new_intervals.back().is_reversed = ZipCodeTree::seed_is_reversed_at_depth(seeds->at(zipcode_sort_order[interval.interval_end-1]), 
                                                                               child_depth, *distance_index) 
                             ? !interval.is_reversed
                             : interval.is_reversed;
#ifdef DEBUG_ZIP_CODE_TREE
    cerr << "New sort order " << endl;
    for (auto& interval : new_intervals) {
        for (size_t i = interval.interval_start ; i < interval.interval_end ; i++) {
            cerr << seeds->at(zipcode_sort_order[i]).pos << ", ";
        }
        cerr << "|";
    }
    cerr << endl;
#endif
    return new_intervals;
}

void ZipCodeForest::radix_sort_zipcodes(vector<size_t>& zipcode_sort_order, const vector<sort_value_t>& sort_values_by_seed,
                                        const interval_and_orientation_t& interval, bool reverse_order,
                                        size_t min_value, size_t max_value) const {
    //Radix sort the interval of zipcode_sort_order in the given interval
#ifdef DEBUG_ZIP_CODE_SORTING
    cerr << "\tradix sort" << endl;
#endif

    //Mostly copied from Jordan Eizenga

    // count up occurrences of each rank
    std::vector<size_t> counts (max_value-min_value+2, 0);
    for (size_t i  = interval.interval_start ; i < interval.interval_end ; i++) {
#ifdef DEBUG_ZIP_CODE_SORTING
        assert(sort_values_by_seed[zipcode_sort_order[i]].get_sort_value() >= min_value);
        assert(sort_values_by_seed[zipcode_sort_order[i]].get_sort_value() <= max_value);
        cerr << "Sort value for seed " << seeds->at(zipcode_sort_order[i]).pos << ": " 
             << sort_values_by_seed[zipcode_sort_order[i]].get_sort_value() << endl;
        assert(counts.size() > sort_values_by_seed[zipcode_sort_order[i]].get_sort_value() - min_value + 1);
#endif
        size_t next_rank = sort_values_by_seed[zipcode_sort_order[i]].get_sort_value() - min_value + 1;

        ++counts[next_rank];
    }
    
    //Make this a count of the number of things before it
    for (size_t i = 1; i < counts.size(); ++i) {
        counts[i] += counts[i - 1];
    }

    //Get the sorted order
    std::vector<size_t> sorted(interval.interval_end - interval.interval_start);
    for (size_t i = interval.interval_start ; i < interval.interval_end ; i++) {
        size_t rank = sort_values_by_seed[zipcode_sort_order[i]].get_sort_value() - min_value;
        sorted[counts[rank]++] = zipcode_sort_order[i];
    }
    
    //And place everything in the correct position
    for (size_t i = 0 ; i < sorted.size() ; i++) {


        //If this is reversed in the top-level chain, then the order should be backwards
        if (reverse_order) {
            zipcode_sort_order[interval.interval_end - i - 1] = sorted[i];
        } else {
            zipcode_sort_order[i + interval.interval_start] = sorted[i];
        }
    }

}
void ZipCodeForest::default_sort_zipcodes(vector<size_t>& zipcode_sort_order, const vector<sort_value_t>& sort_values_by_seed,
                                   const interval_and_orientation_t& interval, bool reverse_order) const { 
    //std::sort the interval of zipcode_sort_order between interval_start and interval_end
    
#ifdef DEBUG_ZIP_CODE_SORTING
    cerr << "\tdefault sort between " << interval.interval_start  << " and " << interval.interval_end  << endl;
    cerr << "\tis rev: " << reverse_order << endl;
#endif
    //Sort using std::sort 
    std::sort(zipcode_sort_order.begin() + interval.interval_start, 
              zipcode_sort_order.begin() + interval.interval_end, [&] (size_t a, size_t b) {
        //If this snarl tree node is reversed, then reverse the sort order
        return reverse_order ? sort_values_by_seed[a].get_sort_value() > sort_values_by_seed[b].get_sort_value()
                             : sort_values_by_seed[a].get_sort_value() < sort_values_by_seed[b].get_sort_value();
    });
}


    
}

namespace std {

std::string to_string(const vg::ZipCodeTree::tree_item_type_t& type) {
    switch (type) {
    case vg::ZipCodeTree::SEED:
        return "SEED";
    case vg::ZipCodeTree::SNARL_START:
        return "SNARL_START";
    case vg::ZipCodeTree::SNARL_END:
        return "SNARL_END";
    case vg::ZipCodeTree::CHAIN_START:
        return "CHAIN_START";
    case vg::ZipCodeTree::CHAIN_END:
        return "CHAIN_END";
    case vg::ZipCodeTree::EDGE:
        return "EDGE";
    case vg::ZipCodeTree::NODE_COUNT:
        return "NODE_COUNT";
    default:
        throw std::runtime_error("Unimplemented zip code tree item type");
    }
}

std::string to_string(const vg::ZipCodeTree::reverse_iterator::State& state) {
    switch (state) {
    case vg::ZipCodeTree::reverse_iterator::S_START:
        return "S_START";
    case vg::ZipCodeTree::reverse_iterator::S_SCAN_CHAIN:
        return "S_SCAN_CHAIN";
    case vg::ZipCodeTree::reverse_iterator::S_STACK_SNARL:
        return "S_STACK_SNARL";
    case vg::ZipCodeTree::reverse_iterator::S_SCAN_SNARL:
        return "S_SCAN_SNARL";
    case vg::ZipCodeTree::reverse_iterator::S_SKIP_CHAIN:
        return "S_SKIP_CHAIN";
    default:
        throw std::runtime_error("Unimplemented zip code tree reverse iterator state");
    }
}



}

