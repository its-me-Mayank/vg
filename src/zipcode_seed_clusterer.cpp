#include "zipcode_seed_clusterer.hpp"

#define DEBUG_ZIPCODE_CLUSTERING

namespace vg {


/*
 * Coarsely cluster the seeds using their zipcodes
 * All seeds start out in the same partition and are split into different partitions according to their position on the snarl tree
 * Seeds are first ordered recursively along the snarl tree - along chains and according to the distance to the start of a snarl.
 * Snarls/chains are found by walking along the ordered list of seeds and processed in a bfs traversal of the snarl tree
 * This is accomplished using a queue of partitioning_problem_t's, which represent the next snarl tree node to partition.
 * All partitions are maintained in a partition_set_t, which is processed into clusters at the end
 */
vector<ZipcodeClusterer::Cluster> ZipcodeClusterer::coarse_cluster_seeds(const vector<Seed>& seeds, size_t distance_limit ) {
#ifdef DEBUG_ZIPCODE_CLUSTERING
    cerr << endl << endl << "New zipcode clustering of " << seeds.size() << " seeds with distance limit" <<  distance_limit << endl;
#endif

    //This holds all the partitions found. It gets processed into clusters at the end
    partition_set_t all_partitions;

    //A queue of everything that needs to be partitioned. Each item represents the seeds in a single snarl tree node
    //The snarl tree gets processed in a bfs traversal
    std::list<partitioning_problem_t> to_partition;

    /* First, initialize the problem with one partition for each connected component
     *
     * Sort the seeds by their position in the snarl tree
     * The seeds are sorted first by connected component, by position along a chain, by the distance to the start of a snarl,
     * and by the rank in the snarl. 
     * Then walk through the ordered list of seeds and add to start/end_count for skipping to the ends of snarl tree nodes, 
     * and split by connected component and create a new partitioning_problem_t in to_partition for each connected component
     */

    //This is the first partition containing all the seeds
    all_partitions.reserve(seeds.size());
    for (size_t i = 0 ; i < seeds.size() ; i++) {
        all_partitions.add_new_item(i);
    }

    //Initialize child_start and child_end bv's
    //TODO: I think this fills it in with 0's
    all_partitions.child_start_bv.resize(seeds.size());
    all_partitions.child_end_bv.resize(seeds.size());

    //Sort
    all_partitions.sort(0, seeds.size(), [&] (const partition_item_t& a, const partition_item_t& b) {
        //Comparator for sorting. Returns a < b
        size_t depth = 0;
        while (depth < seeds[a.seed].zipcode_decoder->decoder_length()-1 &&
               depth < seeds[b.seed].zipcode_decoder->decoder_length()-1 &&
               ZipCodeDecoder::is_equal(*seeds[a.seed].zipcode_decoder, *seeds[b.seed].zipcode_decoder, depth)) {
            depth++;
        }
        //Either depth is the last thing in a or b, or they are different at this depth 
        if ( ZipCodeDecoder::is_equal(*seeds[a.seed].zipcode_decoder, *seeds[b.seed].zipcode_decoder, depth)) {
            //If they are equal, then they must be on the same node

            size_t offset1 = is_rev(seeds[a.seed].pos) 
                           ? seeds[a.seed].zipcode_decoder->get_length(depth) - offset(seeds[a.seed].pos) - 1
                           : offset(seeds[a.seed].pos); 
            size_t offset2 = is_rev(seeds[b.seed].pos) 
                           ? seeds[b.seed].zipcode_decoder->get_length(depth) - offset(seeds[b.seed].pos) - 1
                           : offset(seeds[b.seed].pos); 
            if (depth == 0 || seeds[a.seed].zipcode_decoder->get_code_type(depth-1) == REGULAR_SNARL ||
                 seeds[a.seed].zipcode_decoder->get_code_type(depth-1) == IRREGULAR_SNARL ||
                 seeds[a.seed].zipcode_decoder->get_code_type(depth-1) == ROOT_SNARL ||
                !seeds[a.seed].zipcode_decoder->get_is_reversed_in_parent(depth)) {
                //If they are in a snarl or they are facing forward on a chain, then order by 
                //the offset in the node
                return offset1 < offset2;
            } else {
                //Otherwise, the node is facing backwards in the chain, so order backwards in node
                return offset2 < offset1;
            }
        } else if (depth == 0) {
            //If they are on different connected components, sort by connected component
            return seeds[a.seed].zipcode_decoder->get_distance_index_address(0) < seeds[b.seed].zipcode_decoder->get_distance_index_address(0);
            
        } else if (seeds[a.seed].zipcode_decoder->get_code_type(depth-1) == CHAIN || seeds[a.seed].zipcode_decoder->get_code_type(depth-1) == ROOT_CHAIN) {
            //If a and b are both children of a chain
            size_t offset_a = seeds[a.seed].zipcode_decoder->get_offset_in_chain(depth);
            size_t offset_b = seeds[b.seed].zipcode_decoder->get_offset_in_chain(depth);
            if ( offset_a == offset_b) {
                //If they have the same prefix sum, then the snarl comes first
                return seeds[a.seed].zipcode_decoder->get_code_type(depth) != NODE && seeds[b.seed].zipcode_decoder->get_code_type(depth) == NODE;  
            } else {
                return offset_a < offset_b;
            }
        } else if (seeds[a.seed].zipcode_decoder->get_code_type(depth-1) == REGULAR_SNARL) {
            //If the parent is a regular snarl, then sort by child number
            return seeds[a.seed].zipcode_decoder->get_rank_in_snarl(depth) < seeds[b.seed].zipcode_decoder->get_rank_in_snarl(depth);
        } else {
            //Otherwise, they are children of an irregular snarl
            return seeds[a.seed].zipcode_decoder->get_distance_to_snarl_start(depth) < seeds[b.seed].zipcode_decoder->get_distance_to_snarl_start(depth);
        }
    });

#ifdef DEBUG_ZIPCODE_CLUSTERING
    cerr << "Sorted seeds:" << endl; 
    for (auto& item : all_partitions.data) {
        size_t this_seed = item.seed;
        cerr << seeds[this_seed].pos << endl;
    }
    cerr << endl;
#endif

    //Partition by connected_component and create a new partitioning_problem_t for each
    //Also update to start/end_count for each item. For each seed that is the first seed for a particular child, 
    //store the length of that child and its depth

    //A list of the index of the first seed in a snarl tree node at each depth. This is used to fill in to start/end_count
    //Initialized to be 0 for all snarl tree nodes of the first seed
    std::vector<size_t> first_zipcode_at_depth (seeds[all_partitions.data[0].seed].zipcode_decoder->decoder_length(), 0);

    //The beginning of the connected component we're currently on 
    size_t last_connected_component_start = 0;

    //Add the new partition
    all_partitions.partition_heads.emplace_back(0);


    for (size_t i = 1 ; i < all_partitions.data.size() ; i++ ) {

        auto& current_decoder = *seeds[all_partitions.data[i].seed].zipcode_decoder;
        size_t current_depth = current_decoder.decoder_length();

        //For any snarl tree node that ends here, add it's to start/end_count
        for (int depth = first_zipcode_at_depth.size()-1 ; depth >= 0 ; depth--) {
            if (current_depth > depth ||
                !ZipCodeDecoder::is_equal(current_decoder, *seeds[all_partitions.data[i-1].seed].zipcode_decoder, depth)) {
                //If the previous thing was in a different snarl tree node at this depth

                if (first_zipcode_at_depth[depth] != i-1 ) {
                    //If the first seed in this child wasn't the seed right before this one
                    //Add the number of things that were in that snarl tree node
                    all_partitions.data[first_zipcode_at_depth[depth]].start_count++;
                    cerr << "Adding at " << first_zipcode_at_depth[depth] << " with length " << all_partitions.child_start_bv.size() << endl;
                    all_partitions.child_start_bv[first_zipcode_at_depth[depth]] = 1;

                    all_partitions.data[i].end_count++;
                    all_partitions.child_end_bv[i] = 1;
                }
                first_zipcode_at_depth[depth] = i;

            }
        }
        if (current_depth > first_zipcode_at_depth.size()) {
            //We need to add things
            while (first_zipcode_at_depth.size() <= current_depth) {
                first_zipcode_at_depth.emplace_back(i);
            }
        } else if (current_depth < first_zipcode_at_depth.size()) {
            //We need to remove things
            while (first_zipcode_at_depth.size() > current_depth+1) {
                first_zipcode_at_depth.pop_back();
            }
        }

        //Now check if this is the start of a new connected component
        if (!ZipCodeDecoder::is_equal(*seeds[all_partitions.data[i-1].seed].zipcode_decoder, 
                                      current_decoder, 0)) {
            //If these are on different connected components
#ifdef DEBUG_ZIPCODE_CLUSTERING
            cerr << "New connected component for seeds between " << last_connected_component_start << " and " << i << endl;
#endif

            //Make a new partition at i
            all_partitions.split_partition(i);

            //Remember to partition everything from the start to i-1
            if (i != last_connected_component_start+1) {
                to_partition.push_back({last_connected_component_start, i, 0});
            }

            //i is the new start of the current partition
            last_connected_component_start = i;
             

            //Update the first zipcode at each depth
            first_zipcode_at_depth.assign (current_decoder.decoder_length(), i);
        } else if (i == all_partitions.data.size()-1) {
            //If this was the last one
#ifdef DEBUG_ZIPCODE_CLUSTERING
            cerr << "New connected component for seeds between " << last_connected_component_start << " and " << i << endl;
#endif

            //Remember to partition everything from the start to i-1
            if (i != last_connected_component_start+1) {
                to_partition.push_back({last_connected_component_start, i, 0});
            }

            //i is the new start of the current partition
            last_connected_component_start = i;
             

            //Update the first zipcode at each depth
            first_zipcode_at_depth.assign (current_decoder.decoder_length(), i);
        }
    }

    /*
     * Now go through all the partitioning_problem_t's and solve them
     * partition_by_chain/snarl will add to to_partition as they go
     */
     
    while (!to_partition.empty()) {

        //Get the next problem from the front of the queue 
        const auto& current_problem = to_partition.front();
        //Remove it from the queue
        to_partition.pop_front();

        code_type_t code_type = seeds[all_partitions.data[current_problem.range_start].seed].zipcode_decoder->get_code_type(current_problem.depth);
        cerr << "CODE TYPE " << code_type << endl;

        if (code_type == CHAIN || code_type == NODE || code_type == ROOT_CHAIN) {
            partition_by_chain(seeds, current_problem, all_partitions, to_partition, distance_limit);
        } else {
            partition_by_snarl(seeds, current_problem, all_partitions, to_partition, distance_limit);
        }

    }

    
    /* When there is nothing left in to_partition, partitioning is done.
     * Go through all partitions and create clusters
     */
#ifdef DEBUG_ZIPCODE_CLUSTERING
     cerr << "Final clusters:" << endl;

     //Make sure we included every seed exactly once
     vector<bool> included_seed (seeds.size(), 0);
#endif
     vector<Cluster> all_clusters;
     all_clusters.reserve(all_partitions.partition_heads.size());
     for (const size_t& cluster_head : all_partitions.partition_heads) {
         all_clusters.emplace_back();

         partition_item_t& current_item = all_partitions.data[cluster_head];
         while (current_item.next != std::numeric_limits<size_t>::max()){
#ifdef DEBUG_ZIPCODE_CLUSTERING
             cerr << seeds[current_item.seed].pos << " ";
             assert(included_seed[current_item.seed] == 0);

             included_seed[current_item.seed] = 1;
#endif
            all_clusters.back().seeds.emplace_back(current_item.seed);
            current_item = all_partitions.data[current_item.next]; 
         }
         all_clusters.back().seeds.emplace_back(current_item.seed);
#ifdef DEBUG_ZIPCODE_CLUSTERING
         cerr << seeds[current_item.seed].pos << endl;

         assert(included_seed[current_item.seed] == 0);
         included_seed[current_item.seed] = 1;
#endif
     }
#ifdef DEBUG_ZIPCODE_CLUSTERING
     for (auto x : included_seed) {
         assert(x == 1);
     }
#endif

    return all_clusters;
}

/* Partition the given problem along a chain
 * The seeds in the current_problem must be sorted along the chain
 * Chains are split when the distance between subsequent seeds is definitely larger than the distance_limit
 */

void ZipcodeClusterer::partition_by_chain(const vector<Seed>& seeds, const partitioning_problem_t& current_problem,
    partition_set_t& all_partitions, std::list<partitioning_problem_t>& to_partition,
    const size_t& distance_limit){
#ifdef DEBUG_ZIPCODE_CLUSTERING
    cerr << "Partition " << (current_problem.range_end - current_problem.range_start) << " seeds along a chain at depth " << current_problem.depth << endl;
    assert(current_problem.range_end > current_problem.range_start);
#endif
    const size_t& depth = current_problem.depth;

    //We're going to walk through the seeds on children of the chain, starting from the second one
    size_t previous_index = current_problem.range_start;
    partition_item_t& previous_item = all_partitions.data[previous_index];

    //First, check if we actually have to do any work
    if (previous_item.next == std::numeric_limits<size_t>::max() ||
        (depth > 0 && seeds[previous_item.seed].zipcode_decoder->get_length(depth) <= distance_limit)) {
        //If there was only one seed, or the chain is too short, then don't do anything
        return;
    }
#ifdef DEBUG_ZIPCODE_CLUSTERING
    cerr << "First seed " << seeds[all_partitions.data[previous_index].seed].pos << endl;
#endif

    //Get the index of the next partition_item_t in the chain
    size_t current_index = all_partitions.get_last_index_at_depth(previous_index, depth, seeds);

    //If the first seed was in a snarl with other seeds, then remember to partition the snarl
    if (all_partitions.data[current_index].prev != previous_index) {
        to_partition.push_back({previous_index, all_partitions.data[current_index].prev, depth+1});
    }

    /*Walk through the sorted list of seeds and partition
    */
    while (current_index != std::numeric_limits<size_t>::max()) {

#ifdef DEBUG_ZIPCODE_CLUSTERING
        cerr << "At seed " << seeds[all_partitions.data[current_index].seed].pos << endl;
#endif
        auto& curr_decoder = *(seeds[all_partitions.data[current_index].seed].zipcode_decoder);
        auto& prev_decoder = *( seeds[all_partitions.data[previous_index].seed].zipcode_decoder);

        //Get the values we need to calculate distance
        size_t current_prefix_sum = curr_decoder.get_offset_in_chain(depth+1);
        size_t previous_prefix_sum = prev_decoder.get_offset_in_chain(depth+1);

        //If these are nodes, add the offsets of the positions
        if (curr_decoder.get_code_type(depth+1) == NODE) {
            current_prefix_sum = SnarlDistanceIndex::sum(current_prefix_sum,
                curr_decoder.get_is_reversed_in_parent(depth+1) 
                    ? curr_decoder.get_length(depth+1) 
                      - offset(seeds[all_partitions.data[current_index].seed].pos)
                    : offset(seeds[all_partitions.data[current_index].seed].pos)+1
            );
        }
        if (prev_decoder.get_code_type(depth+1) == NODE) {
            previous_prefix_sum = SnarlDistanceIndex::sum(previous_prefix_sum,
                prev_decoder.get_is_reversed_in_parent(depth+1) 
                    ? prev_decoder.get_length(depth+1) 
                      - offset(seeds[all_partitions.data[previous_index].seed].pos)
                    : offset(seeds[all_partitions.data[previous_index].seed].pos)+1
            );
        }

        //If these are on different children, add the length of the previous one
        if (!ZipCodeDecoder::is_equal(prev_decoder, curr_decoder, depth+1)) {
            previous_prefix_sum= SnarlDistanceIndex::sum(previous_prefix_sum, 
                                                         prev_decoder.get_length(depth+1)); 
        }

        if (previous_prefix_sum != std::numeric_limits<size_t>::max() &&
            current_prefix_sum != std::numeric_limits<size_t>::max() &&
            SnarlDistanceIndex::minus(current_prefix_sum, previous_prefix_sum) 
                   > distance_limit) {

#ifdef DEBUG_ZIPCODE_CLUSTERING
            cerr << "\tthis is too far from the last seed so make a new cluster" << endl;
            cerr << "\tLast prefix sum: " << previous_prefix_sum <<  " this prefix sum: " << current_prefix_sum << endl;
#endif
            //If too far from the last seed, then split off a new cluster
            all_partitions.split_partition(current_index);
        }
#ifdef DEBUG_ZIPCODE_CLUSTERING
        else {
            cerr << "\tthis is close enough to the last thing, so it is in the same cluster" << endl;
            cerr << "\tLast prefix sum: " << previous_prefix_sum <<  " this prefix sum: " << current_prefix_sum << endl;
        }
#endif

        //Update to the next thing in the list
        previous_index = current_index;

        //Check if this was the last thing in the range
        if (current_index == current_problem.range_end) {
            //If this is the last thing we wanted to process
            current_index = std::numeric_limits<size_t>::max();
        } else {
            //Otherwise, get the next thing, skipping other things in the same child at this depth
            current_index = all_partitions.get_last_index_at_depth(previous_index, depth+1, seeds);

            //If this skipped a snarl in the chain, then remember to cluster it later
            if (all_partitions.data[current_index].prev != previous_index) {
                to_partition.push_back({previous_index, all_partitions.data[current_index].prev, depth+1});
            }

#ifdef DEBUG_ZIPCODE_CLUSTERING
            if (current_index == std::numeric_limits<size_t>::max()) {
                assert(previous_index == current_problem.range_end);
            }
#endif
        }
    }

    return;
}

/*
 * Snarls are processed in two passes over the seeds. First, they are sorted by the distance to the start of the snarl and
 * split if the difference between the distances to the start is greater than the distance limit
 * Then, all seeds are then sorted by the distance to the end of the snarl and edges in the linked list are added back
 * if the distance is small enough between subsequent seeds

 * Finally, the leftmost and rightmost seeds in the snarl are checked against the next things in the parent chain,
 * and possibly disconnected
 * Proof: For each child, x, in a snarl, we know the minimum distance to the start and end boundary nodes of the snarl (x_start and x_end)
 * For two children of the snarl, x and y, assume that x_start <= y_start.
 * Then there can be no path from x to y that is less than (y_start - x_start), otherwise y_start would be smaller. 
 * So y_start-x_start is a lower bound of the distance from x to y
 */
void ZipcodeClusterer::partition_by_snarl(const vector<Seed>& seeds, const partitioning_problem_t& current_problem,
    partition_set_t& all_partitions, std::list<partitioning_problem_t>& to_partition,
    const size_t& distance_limit){

#ifdef DEBUG_ZIPCODE_CLUSTERING
    cerr << "Partition " << (current_problem.range_end - current_problem.range_start) << " seeds along a snarl at depth " << current_problem.depth << endl;
    assert(current_problem.range_end > current_problem.range_start);
#endif

    const size_t& depth = current_problem.depth;


    if (depth == 0) {
        //If this is a top-level snarl, then we don't have distances to the starts and ends so everything 
        //is in one cluster
        //Go through the children and remember to partition each child
#ifdef DEBUG_ZIPCODE_CLUSTERING
        cerr << "This is a top-level snarl, so just remember to partition the children" << endl;
#endif
        size_t previous_index = current_problem.range_start;

        //Get the index of the first partition_item_t of the next snarl child
        size_t current_index = all_partitions.get_last_index_at_depth(previous_index, depth+1, seeds);


        while (current_index != std::numeric_limits<size_t>::max()) {

            //Update to the next thing in the list
            previous_index = current_index;

            //Check if this was the last thing in the range
            if (current_index == current_problem.range_end) {
                //If this is the last thing we wanted to process
                current_index = std::numeric_limits<size_t>::max();
            } else {
                //Otherwise, get the next thing, skipping other things in the same child at this depth
                current_index = all_partitions.get_last_index_at_depth(previous_index, depth+1, seeds);

                //If this skipped a snarl in the chain, then remember to cluster it later
                //and add everything in between to the union find
                if (all_partitions.data[current_index].prev != previous_index) {
                    //Remember to partition it
                    to_partition.push_back({previous_index, all_partitions.data[current_index].prev, depth+1});
                }

#ifdef DEBUG_ZIPCODE_CLUSTERING
                if (current_index == std::numeric_limits<size_t>::max()) {
                    assert(previous_index == current_problem.range_end);
                }
#endif
            }
        }
        return;
    }
    /* 
      To merge two partitions in the second phase, we need to be able to quickly find the 
      head and tails of two partitions.
      This will be done using a rank-select bit vector that stores the locations of every
      head of lists in the first phase, not necessarily including the first and last seeds.
      The sorting is done using a list of indices, rather than re-ordering the seeds,
      so none of the seeds will move around in the vector all_partitions.data
      All pointers will stay valid, and we can ensure that the heads of linked lists
      always precede their tails in the vector. 
      When finding the head of a linked list, use the rank-select bv to find the original
      head of the item, going left in the vector. 
      If its prev pointer points to null, then it is the head. 
      Otherwise, follow the prev pointer and find the next earlier thing 
    */

    //This will hold a 1 for each position that is the head of a linked list
    //Tails will always be at the preceding index
    sdsl::bit_vector list_heads (current_problem.range_end - current_problem.range_start);


    //A vector of indices into all_partitions.data, only for the children in the current problem
    //This gets sorted by distance to snarl end for the second pass over the seeds
    //This will include one seed for each child, since we will be able to find the head/tail of
    //any linked list from any of its members
    //This will be a pair of the index into all_partitions.data, the distance to the end
    vector<pair<size_t, size_t>> sorted_indices;
    sorted_indices.reserve (current_problem.range_end - current_problem.range_start);

    //We're going to walk through the seeds on children of the snarl, starting from the second one
    size_t previous_index = current_problem.range_start;
    partition_item_t& previous_item = all_partitions.data[previous_index];

    //First, check if we actually have to do any work
    if (previous_item.next == std::numeric_limits<size_t>::max() ||
        (depth > 0 && seeds[previous_item.seed].zipcode_decoder->get_length(depth) <= distance_limit)) {
        //If there was only one seed, or the snarl is too short, then don't do anything
        //TODO: If there was only one seed, still need to check if it should remain connected to the previous
        //and next things in the chain
        return;
    }

    //Get the index of the first partition_item_t of the next snarl child
    size_t current_index = all_partitions.get_last_index_at_depth(previous_index, depth+1, seeds);

    //If the first seed was in a chain with other seeds, then remember to partition the chain later
    if (all_partitions.data[current_index].prev != previous_index) {
        to_partition.push_back({previous_index, all_partitions.data[current_index].prev, depth+1});
    }


    //Go through the list forwards, and at each item, either partition or add to the union find
    while (current_index != std::numeric_limits<size_t>::max()) {

#ifdef DEBUG_ZIPCODE_CLUSTERING
        cerr << "At seed " << seeds[all_partitions.data[current_index].seed].pos << endl;
#endif

        //Remember that we need to include this in the second pass
        sorted_indices.emplace_back(current_index, seeds[all_partitions.data[current_index].seed].zipcode_decoder->get_distance_to_snarl_end(depth+1));

        //Get the values we need to calculate distance
        size_t current_distance_to_start = seeds[all_partitions.data[current_index].seed].zipcode_decoder->get_distance_to_snarl_start(depth+1);
        size_t previous_distance_to_start = seeds[all_partitions.data[previous_index].seed].zipcode_decoder->get_distance_to_snarl_start(depth+1);
        size_t previous_length = seeds[all_partitions.data[previous_index].seed].zipcode_decoder->get_length(depth+1);

        if (previous_distance_to_start != std::numeric_limits<size_t>::max() &&
            current_distance_to_start != std::numeric_limits<size_t>::max() &&
            SnarlDistanceIndex::minus(current_distance_to_start,
                                      SnarlDistanceIndex::sum(previous_distance_to_start, previous_length)) 
                   > distance_limit) {

#ifdef DEBUG_ZIPCODE_CLUSTERING
            cerr << "\tthis is too far from the last seed so make a new cluster" << endl;
            cerr << "\tLast distance_to_start: " << previous_distance_to_start << " last length " << previous_length << " this distance to start: " << current_distance_to_start << endl;
#endif
            //If too far from the last seed, then split off a new cluster
            all_partitions.split_partition(current_index);

            //ALso update the bitvector with the locations of the new head
            list_heads[current_index - current_problem.range_start] = 1;
        } 
#ifdef DEBUG_ZIPCODE_CLUSTERING
        else {
            cerr << "\tthis is close enough to the last thing, so it is in the same cluster" << endl;
            cerr << "\tLast distance to start: " << previous_distance_to_start << " last length " << previous_length << " this distance to start: " << current_distance_to_start << endl;
        }
#endif

        //Update to the next thing in the list
        previous_index = current_index;

        //Check if this was the last thing in the range
        if (current_index == current_problem.range_end) {
            //If this is the last thing we wanted to process
            current_index = std::numeric_limits<size_t>::max();
        } else {
            //Otherwise, get the next thing, skipping other things in the same child at this depth
            current_index = all_partitions.get_last_index_at_depth(previous_index, depth+1, seeds);

            //If this skipped a snarl in the chain, then remember to cluster it later
            //and add everything in between to the union find
            if (all_partitions.data[current_index].prev != previous_index) {
                //Remember to partition it
                to_partition.push_back({previous_index, all_partitions.data[current_index].prev, depth+1});
            }

#ifdef DEBUG_ZIPCODE_CLUSTERING
            if (current_index == std::numeric_limits<size_t>::max()) {
                assert(previous_index == current_problem.range_end);
            }
#endif
        }
    }

    /* Finished going through the list of children by distance to start
       Now sort it again and go through it by distance to end, 
       adding back connections if they are close enough
    */


    //Initialize the rank and select vectors
    sdsl::rank_support_v<1> list_heads_rank(&list_heads);
    sdsl::select_support_mcl<1> list_heads_select(&list_heads);

    //First, add support for finding the heads and tails of linked lists

    //Given an index into all_partitions.data (within the current problem range), return 
    //the head of the 
    auto get_list_head = [&] (size_t index) {
        while (all_partitions.data[index].prev != std::numeric_limits<size_t>::max() 
               && index != current_problem.range_start) {
            size_t rank = list_heads_rank(index);
            size_t head_index = list_heads_select(rank);
            if (head_index == current_problem.range_start ||
                all_partitions.data[head_index].prev == std::numeric_limits<size_t>::max()) {
                //If this is a head, then return
                return head_index;
            } else {
                //If this is no longer a head, go back one and try again
                index = all_partitions.data[head_index].prev;
            }
        }
        return index;
    };
    auto get_list_tail = [&] (size_t index) {
        while (all_partitions.data[index].next != std::numeric_limits<size_t>::max() 
               && index != current_problem.range_end) {
            size_t rank = list_heads_rank(index);
            size_t tail_index = list_heads_select(rank+1)-1;
            if (tail_index == current_problem.range_end ||
                all_partitions.data[tail_index].next == std::numeric_limits<size_t>::max()) {
                //If this is already a tail, then return
                return tail_index;
            } else {
                //If this is no longer a tail, go forwards one and try again
                index = all_partitions.data[tail_index].next;
            }
        }
        return index;
    };


    //Sort sorted indices by the distance to the end of the snarl
    std::sort(sorted_indices.begin(), sorted_indices.end(), [&] (const pair<size_t, size_t>& a, const pair<size_t, size_t>& b) {
        //Comparator for sorting. Returns a < b
        return a.second < b.second;
    });

    //Go through sorted_indices, and if two consecutive items are close, merge them
    //Merging must guarantee that the head of a list is always before the tail in the vector
    for (size_t i = 1 ; i < sorted_indices.size() ; i++ ) {

        //Get the heads of the two linked lists
        size_t head1 = get_list_head(sorted_indices[i-1].first); 
        size_t head2 = get_list_head(sorted_indices[i].first);
        if (head1 != head2) {
            //If they are the same list, then do nothing. Otherwise, compare them
            if (sorted_indices[i].second - sorted_indices[i-1].second < distance_limit) {
                //They are close so merge them
                size_t tail1 = get_list_tail(sorted_indices[i-1].first); 
                size_t tail2 = get_list_tail(sorted_indices[i].first);
                if (head1 < head2 && tail1 > tail2) {
                    //If the second list is entirely contained within the first
                    //Arbitrarily add it to the end of the first section of the first list
                    //(the portion that was a list before it got combined with something else
                    size_t new_tail = list_heads_select(list_heads_rank(head1)+1)-1;
                    size_t new_head = all_partitions.data[new_tail].next;

                    //Now reattach the second list to new_head/tail
                    all_partitions.data[new_tail].next = head2;
                    all_partitions.data[head2].prev = new_tail;

                    all_partitions.data[new_head].prev = tail2;
                    all_partitions.data[tail2].next = new_head;

                } else if (head1 < head2 && tail1 > tail2) {
                    //If the first list is entirely contained within the second 
                    //Add the first list to the end of the first section of the second list
                    size_t new_tail = list_heads_select(list_heads_rank(head2)+1)-1;
                    size_t new_head = all_partitions.data[new_tail].next;

                    //Reattach the first list to the new head/tail
                    all_partitions.data[new_tail].next = head1;
                    all_partitions.data[head1].prev = new_tail;

                    all_partitions.data[new_head].prev = tail1;
                    all_partitions.data[tail1].next = new_head;
                } else if (head1 < head2) {
                    //If the first list is before the second
                    all_partitions.data[head2].prev = tail1;
                    all_partitions.data[tail1].next = head2;

                } else {
                    //if the second list is before the first
                    all_partitions.data[head1].prev = tail2;
                    all_partitions.data[tail2].next = head1;
                }

            }
        }
    }


    /* Finished going through the list of children by distance to end
    */
    
}

ZipcodeClusterer::partition_set_t::partition_set_t() {
}

//Move constructor
//ZipcodeClusterer::partition_set_t::partition_set_t(partition_set_t&& other) :
//    data(std::move(other.data)), head(other.head), tail(other.tail) {
//    other.data = std::vector<partition_item_t>(0);
//    other.head = nullptr;
//    other.tail = nullptr; 
//}

void ZipcodeClusterer::partition_set_t::add_new_item(size_t value) {
    data.push_back({value, 
                      std::numeric_limits<size_t>::max(), 
                      std::numeric_limits<size_t>::max()});
}
void ZipcodeClusterer::partition_set_t::reserve(const size_t& size) {
    data.reserve(size);
}


size_t ZipcodeClusterer::partition_set_t::get_last_index_at_depth(const size_t& current_index, 
        const size_t& depth, const vector<Seed>& seeds) {
    partition_item_t& current_item = data[current_index];
    if (current_item.start_count == 0) {
        //If this is not the start of any run of seeds
        return current_item.next;
    } else if (!ZipCodeDecoder::is_equal(*seeds[data[current_item.next].seed].zipcode_decoder, 
                                         *seeds[current_item.seed].zipcode_decoder, depth)) {
        //If this is the start of a run of seeds, but this is a different child than the next thing at this depth
        return current_item.next;
    } else {
        //This is the start of a run of seeds at this depth.
        //Walk through the child_start_bv and child_end bv to find the end of this run at this depth

        //This is analogous to the parentheses matching problem. Start with a count of how many
        //parentheses were opened here, and keep incrementing/decrementing until it reaches 0 and
        //we've found the matching parenthesis


        size_t parentheses_opened = data[current_index].start_count;

        //Get the next seed with a start parenthesis
        size_t start_rank = child_start_rank(current_index) + 1;
        size_t start_index = child_start_select(start_rank);
        //Get the next seed with an end parenthesis
        size_t end_rank = child_end_rank(current_index) + 1;
        size_t end_index = child_end_select(end_rank);


        while (parentheses_opened > 0) {
            //Check the next seed of interest, which may start or end a run, and update parentheses_opened
            if (start_index < end_index) {
                //count the number of parentheses opened 
                parentheses_opened += data[start_index].start_count;

                //Update to the next seed with a parentheses open
                start_rank++;
                start_index = child_start_select(start_rank);
            } else if (start_index > end_index) {
#ifdef DEBUG_ZIPCODE_CLUSTERING
                assert (parentheses_opened >= data[end_index].end_count);
#endif
                parentheses_opened -= data[end_index].end_count;

                //Update to the next seed with a parentheses close
                end_rank++;
                end_index = child_end_select(end_rank);
            } else {
                //Parentheses are both opened and closed
                //TODO: idk about the order of this
                parentheses_opened += data[start_index].start_count;
                parentheses_opened -= data[end_index].end_count;

                //Update to the next seed with a parentheses open
                start_rank++;
                start_index = child_start_select(start_rank);

                //Update to the next seed with a parentheses close
                end_rank++;
                end_index = child_end_select(end_rank);
            }
        }

        //Decrement the counts of runs at the start and end
        data[current_index].start_count--;
        data[end_index].end_count--;

        return end_index;
    }
}


void ZipcodeClusterer::partition_set_t::sort(size_t range_start, size_t range_end, std::function<bool(const partition_item_t& a, const partition_item_t& b)> cmp, bool reconnect) {


    //Sort the vector
    std::stable_sort(data.begin()+range_start, data.begin()+range_end, cmp);

    if (!reconnect) {
        //If we don't need to reconnect the list, then we're done
        return;
    }

    //Connections to outside of the range. May be max() if the start or end of a list was in the range
    size_t prev, next;

    //If the start of list containing the range was in the range, 
    //then we need to replace it as the start of a list in partitions
    size_t old_start = std::numeric_limits<size_t>::max();


    for (size_t i = 0 ; i < data.size() ; i++) {
        //Go through everything and make it point to the next thing

        //Remember if anything pointed to outside the range
        if (data[i].prev == std::numeric_limits<size_t>::max()) {
            old_start = i;
            prev = std::numeric_limits<size_t>::max();
        } else if (data[i].prev < range_start) {
            prev = data[i].prev;
        }
        if (data[i].next > range_end || data[i].next == std::numeric_limits<size_t>::max()) {
            next = data[i].next;
        }
    
        data[i].prev = i == 0 ? std::numeric_limits<size_t>::max() : i-1;
        data[i].next = i == data.size()-1 ? std::numeric_limits<size_t>::max() : i+1;
    }

    if (prev != std::numeric_limits<size_t>::max()) {
        //If the start of the list was outside the range

        //Make sure the list is connected from the start
        data[prev].next = range_start;
        data[range_start].prev = prev;
    } else {
        //If the start of the list was in the range, then we need to replace the start of the linked list in partition_heads 
        for (size_t i = 0 ; i < partition_heads.size() ; i++) {
            if (partition_heads[i] == old_start) {
                cerr << "REPLACE PARTITION HEAD " << old_start << " WITH " << range_start << endl;
                partition_heads[i] = range_start;
                break;
            }
        }
    }

    if (next != std::numeric_limits<size_t>::max()) {
        // If the end of the list was outside the range, update the end
        data[next].prev = range_end;
        data[range_end].next = next;
    }
    

    

    return;
}

void ZipcodeClusterer::partition_set_t::split_partition(size_t range_start) {
    if (data[range_start].prev == std::numeric_limits<size_t>::max()) {
        //If this is the first thing in a list
        return;
    } else {
        //Otherwise, tell the previous thing that it's now the end of a linked list, and add this one as a new partition

        //Update previous to be the last thing in it's list
        data[data[range_start].prev].next = std::numeric_limits<size_t>::max();

        //Tell range_start that it's the start of a list
        data[range_start].prev = std::numeric_limits<size_t>::max();

        //Add range_start as a new partition
        partition_heads.emplace_back(range_start);
        
    }
}

void ZipcodeClusterer::partition_set_t::split_partition(size_t range_start, size_t range_end) {
    if (data[range_start].prev == std::numeric_limits<size_t>::max() && data[range_end].next == std::numeric_limits<size_t>::max()) {
        //If this is the whole list
        return;
    } else if (data[range_start].prev == std::numeric_limits<size_t>::max()) {
        //If this is the start of an existing list, then start a new one after range_end

        //Update the next head to know it's a head
        data[ data[range_end].next ].prev = std::numeric_limits<size_t>::max();

        //Tell range_end that it's now the end
        data[range_end].next = std::numeric_limits<size_t>::max();

        //Add the next thing as a new partition
        partition_heads.emplace_back(range_end+1);
    } else if (data[range_end].next == std::numeric_limits<size_t>::max()) {
        //This is the end of a partition
        split_partition(range_start);
    } else {
        //Otherwise, this is in the middle of a partition and we need to update the previous and next things to point to each other

        //Update previous and next things to point to each other
        size_t previous = data[range_start].prev;
        size_t next = data[range_end].next;

        data[previous].next = next;
        data[next].prev = previous;

        //Tell range_start and range end that they're the start/end of a list
        data[range_start].prev = std::numeric_limits<size_t>::max();
        data[range_end].next = std::numeric_limits<size_t>::max();

        //Add range_start as a new partition
        partition_heads.emplace_back(range_start);
        
    }
}


}
