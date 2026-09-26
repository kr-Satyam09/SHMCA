#pragma once
#include <vector>

/*
     DSA Unit 2: Circular Queue
     OOP Unit 5: Function templates
*/


template <class T>
class CircularQueue {
    std::vector<T> data;
    int maxSize;
    int start;

    public:
        CircularQueue(int size = 10) {
            maxSize = size;
            start = 0;
        }

        void push(T value) {
            if (data.size() < maxSize) {
                data.push_back(value);
            } else {
                data[start] = value;
                start = (start + 1) % maxSize;
            }
        }

        std::vector<T> getAll() {
            std::vector<T> result;
            for (int i = 0; i < data.size(); i++) {
                int idx = (start + i) % data.size();
                result.push_back(data[idx]);
            }
            return result;
        }
};