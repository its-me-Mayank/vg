#ifndef VG_ZIP_CODE_TREE_HPP_INCLUDED

#define VG_ZIP_CODE_TREE_HPP_INCLUDED

#include "zip_code.hpp"
#include "snarl_seed_clusterer.hpp"

#include <stack>

namespace vg{
using namespace std;

/**

A ZipCodeTree takes a set of SnarlDistanceIndexCluserer::Seed's (seed alignments between a read and reference) 
and provides an iterator that, given a seed and a distance limit, iterates through seeds that are
reachable within the distance limit

Generally, this will take a collection of seeds and build a tree structure representing the connectivity
of the seeds, based on the snarl decomposition
Edges are labelled with distance values.
The tree can be traversed to find distances between seeds
*/
class ZipCodeTree {

    typedef SnarlDistanceIndexClusterer::Seed Seed;

    public:

    /**
     * Constructor
     * The constructor creates a tree of the input seeds that is used for calculating distances
     */
    ZipCodeTree(vector<Seed>& seeds, const SnarlDistanceIndex& distance_index);

    private:

    //The seeds to that are taken as input
    //The order of the seeds will never change, but the vector is not const because the zipcodes
    //decoders may change
    vector<Seed>& seeds;


    /*
      The tree will represent the seeds' placement in the snarl tree 
      Each node in the tree is either a seed (position on the graph) or the boundary of a snarl
      Edges are labelled with the distance between the two nodes

      This graph is actually represented as a vector of the nodes and edges
      Each item in the vector represents either a node (seed or boundary) or an edge (distance)
      TODO: Fill in a description once it's finalized more
     */

    enum tree_item_type_t {SEED, SNARL_START, SNARL_END, CHAIN_START, CHAIN_END, EDGE, NODE_COUNT};
    struct tree_item_t {

        //Is this a seed, boundary, or an edge
        tree_item_type_t type;

        //For a seed, the index into seeds
        //For an edge, the distance value
        //Empty for a bound
        size_t value;
    };

    //The actual tree structure
    vector<tree_item_t> zip_code_tree;

public:
    /**
     * Iterator that visits all seeds right to left in the tree's in-order traversal.
     */
    class iterator {
    public:
        /// Advance right
        iterator& operator++();

        /// Compare for equality to see if we hit end
        bool operator==(const iterator& other) const;

        /// Compare for inequality
        inline bool operator!=(const iterator& other) const {
            return !(*this == other);
        }
        
        /// Get the index of the seed we are currently at.
        size_t operator*() const;

        /// Make an iterator wrapping the given iterator, until the given end.
        iterator(vector<tree_item_t>::iterator it, vector<tree_item_t>::iterator end);

    private:
        /// Where we are in the stored tree.
        vector<tree_item_t>::iterator it;
        /// Where the stored tree ends. We keep this to avoid needing a reference back to the ZipCodeTree.
        vector<tree_item_t>::iterator end;
    };

    /// Get an iterator over indexes of seeds in the tree, left to right.
    iterator begin() const;
    /// Get the end iterator for seeds in the tree, left to right.
    iterator end() const;

    /**
     * Iterator that looks left in the tree from a seed, possibly up to a maximum base distance.
     *
     * See https://github.com/benedictpaten/long_read_giraffe_chainer_prototype/blob/b590c34055474b0c901a681a1aa99f1651abb6a4/zip_tree_iterator.py.
     */
    class reverse_iterator {
    public:
        /// Move left
        reverse_iterator& operator++();

        /// Compare for equality to see if we hit end (the past-the-left position)
        bool operator==(const reverse_iterator& other) const;

        /// Compare for inequality
        inline bool operator!=(const reverse_iterator& other) const {
            return !(*this == other);
        }
        
        /// Get the index of the seed we are currently at, and the distance to it.
        std::pair<size_t, size_t> operator*() const;

        /// Make a reverse iterator wrapping the given reverse iterator, until
        /// the given rend, with the given distance limit.
        reverse_iterator(vector<tree_item_t>::reverse_iterator it, vector<tree_item_t>::reverse_iterator rend, size_t distance_limit);

    private:
        /// Where we are in the stored tree.
        vector<tree_item_t>::reverse_iterator it;
        /// Where the rend is where we have to stop
        vector<tree_item_t>::reverse_iterator rend;
        /// Distance limit we will go up to
        size_t distance_limit;
        /// Stack for computing distances
        std::stack<size_t> stack;

        // Now we define a mini stack language so we can do a
        // not-really-a-pushdown-automaton to parse the distance strings.
    
        /// Push a value to the stack
        void push(size_t value);

        /// Pop a value from the stack
        size_t pop();

        /// Duplicate the top item on the stack
        void dup();

        /// Check stack depth
        size_t depth() const;

        /// Reverse the top n elements of the stack
        void reverse(size_t depth);

        /// Type for the state of the
        /// I-can't-believe-it's-not-a-pushdown-automaton
        enum State {
            S_START,
            S_SCAN_CHAIN,
            S_STACK_SNARL,
            S_SCAN_SNARL
        };

        /// Current state of the automaton
        State current_state;

        /// Tick the automaton, looking at the symbol at *it and updating the
        /// stack and current_state.
        void tick();

    };

};
}
#endif
