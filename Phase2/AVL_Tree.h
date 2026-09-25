#pragma once
#include <vector>
#include <cmath>

// DSA Unit 4: AVL Trees node structure
struct AVLNode {
    long long key;
    std::vector<int> debrisIds;
    AVLNode *left;
    AVLNode *right;
    int height;
};

// It generates a unique sector key for spatial mapping
inline long long getSectorKey(Vector3 p) {
    long long bx = (long long) std::floor(p.x / 100.0);
    long long by = (long long) std::floor(p.y / 100.0);
    long long bz = (long long) std::floor(p.z / 100.0);
    return bx * 1000000LL + by * 1000LL + bz;
}