#include <iostream>
#include <string>
#include <vector>
#include <fstream>
#include "CircularQueue.h"
using namespace std;

struct Vector3D 
{
    double x, y, z;
};

struct HealthStatus
{
    float integrity;
    bool isCompromised;
    float systemHealth;
};